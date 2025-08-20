#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "ADC_DMA_CONTINUOUS";

// ADC 설정
#define ADC_UNIT                    ADC_UNIT_1
#define ADC_CONV_MODE               ADC_CONV_SINGLE_UNIT_1
#define ADC_OUTPUT_TYPE             ADC_DIGI_OUTPUT_FORMAT_TYPE1
#define ADC_ATTEN                   ADC_ATTEN_DB_12
#define ADC_BITWIDTH                ADC_BITWIDTH_12   // ESP32에서 지원하는 명시적 비트폭

// ADC 채널 설정
#define ADC_CHANNEL_NUM             2
#define ADC_CHANNEL_0               ADC_CHANNEL_6  // GPIO34 (기존 하드웨어와 맞춤)
#define ADC_CHANNEL_1               ADC_CHANNEL_7  // GPIO35 (ADC1만 사용)

// DMA 버퍼 설정
#define ADC_BUFFER_SIZE             256   // 버퍼 크기 줄여서 안정성 향상
#define ADC_SAMPLE_FREQ_HZ          20000  // 20kHz 샘플링 (대역폭 향상)

// ADC 결과 구조체
typedef struct {
    uint32_t channel_0_data[ADC_BUFFER_SIZE];
    uint32_t channel_1_data[ADC_BUFFER_SIZE];
    uint32_t buffer_index;
    bool buffer_full;
} adc_dma_data_t;

// 전역 변수
static adc_continuous_handle_t adc_handle = NULL;
static adc_cali_handle_t adc1_cali_handle = NULL;
static adc_dma_data_t adc_data = {0};
static SemaphoreHandle_t adc_data_mutex = NULL;
static QueueHandle_t adc_queue = NULL;
static bool adc_continuous_running = false;

// ADC Continuous Mode 콜백 함수
static bool IRAM_ATTR s_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)
{
    BaseType_t must_yield = pdFALSE;
    
    // ADC 데이터를 큐에 전송
    if (xQueueSendFromISR(adc_queue, edata, &must_yield) != pdTRUE) {
        ESP_LOGE(TAG, "ADC queue full, data lost");
    }
    
    return (must_yield == pdTRUE);
}

// ADC 캘리브레이션 초기화
static esp_err_t adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ADC Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return ret;
}

// ADC Continuous Mode 초기화
esp_err_t adc_dma_continuous_init(void)
{
    esp_err_t ret = ESP_OK;
    
    // 뮤텍스와 큐 생성
    adc_data_mutex = xSemaphoreCreateMutex();
    if (adc_data_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    adc_queue = xQueueCreate(10, sizeof(adc_continuous_evt_data_t));
    if (adc_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue");
        vSemaphoreDelete(adc_data_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    // ADC Continuous Mode 설정
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = ADC_BUFFER_SIZE * 4,  // 더 큰 버퍼로 오버플로우 방지
        .conv_frame_size = ADC_BUFFER_SIZE,
    };
    ret = adc_continuous_new_handle(&adc_config, &adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ADC continuous handle: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // ADC 채널 설정
    adc_digi_pattern_config_t adc_pattern[ADC_CHANNEL_NUM] = {
        {
            .atten = ADC_ATTEN,
            .channel = ADC_CHANNEL_0,
            .unit = ADC_UNIT,
            .bit_width = ADC_BITWIDTH,
        },
        {
            .atten = ADC_ATTEN,
            .channel = ADC_CHANNEL_1,
            .unit = ADC_UNIT,
            .bit_width = ADC_BITWIDTH,
        },
    };
    
    adc_continuous_config_t dig_cfg = {
        .pattern_num = ADC_CHANNEL_NUM,
        .adc_pattern = adc_pattern,
        .sample_freq_hz = ADC_SAMPLE_FREQ_HZ,
        .conv_mode = ADC_CONV_MODE,
        .format = ADC_OUTPUT_TYPE,
    };
    
    ret = adc_continuous_config(adc_handle, &dig_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC continuous: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // ADC 캘리브레이션 초기화
    ret = adc_calibration_init(ADC_UNIT, ADC_CHANNEL_0, ADC_ATTEN, &adc1_cali_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ADC calibration failed, continuing without calibration");
    }
    
    // ADC 이벤트 콜백 등록
    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = s_conv_done_cb,
    };
    ret = adc_continuous_register_event_callbacks(adc_handle, &cbs, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register ADC callbacks: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ESP_LOGI(TAG, "ADC DMA Continuous Mode initialized successfully");
    return ESP_OK;
    
cleanup:
    if (adc_handle) {
        adc_continuous_deinit(adc_handle);
        adc_handle = NULL;
    }
    if (adc_data_mutex) {
        vSemaphoreDelete(adc_data_mutex);
        adc_data_mutex = NULL;
    }
    if (adc_queue) {
        vQueueDelete(adc_queue);
        adc_queue = NULL;
    }
    return ret;
}

// ADC 데이터 처리 태스크
static void adc_data_process_task(void *pvParameters)
{
    adc_continuous_evt_data_t evt_data;
    
    ESP_LOGI(TAG, "ADC data processing task started");
    
    while (adc_continuous_running) {
        if (xQueueReceive(adc_queue, &evt_data, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (xSemaphoreTake(adc_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                // ESP32 ADC continuous mode에서는 각 샘플이 16비트
                uint16_t *data = (uint16_t *)evt_data.conv_frame_buffer;
                uint32_t samples = evt_data.size / sizeof(uint16_t);
                
                // 채널별로 데이터 분리 (교대로 들어옴)
                for (int i = 0; i < samples && adc_data.buffer_index < ADC_BUFFER_SIZE; i += 2) {
                    if (i + 1 < samples) {
                        // ESP32 ADC continuous mode 데이터 형식에 맞게 추출
                        uint16_t raw0 = data[i];
                        uint16_t raw1 = data[i + 1];
                        
                        // 채널 정보와 ADC 값 추출 (ESP32 specific format)
                        uint32_t adc_val0 = raw0 & 0xFFF;  // 12비트 ADC 값
                        uint32_t adc_val1 = raw1 & 0xFFF;  // 12비트 ADC 값
                        
                        adc_data.channel_0_data[adc_data.buffer_index] = adc_val0;
                        adc_data.channel_1_data[adc_data.buffer_index] = adc_val1;
                        adc_data.buffer_index++;
                        
                        if (adc_data.buffer_index >= ADC_BUFFER_SIZE) {
                            adc_data.buffer_full = true;
                            adc_data.buffer_index = 0;  // 버퍼 재사용
                        }
                    }
                }
                xSemaphoreGive(adc_data_mutex);
            }
        }
    }
    
    ESP_LOGI(TAG, "ADC data processing task ended");
    vTaskDelete(NULL);
}

// ADC Continuous Mode 시작
esp_err_t adc_dma_continuous_start(void)
{
    if (adc_handle == NULL) {
        ESP_LOGE(TAG, "ADC handle not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = adc_continuous_start(adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start ADC continuous: %s", esp_err_to_name(ret));
        return ret;
    }
    
    adc_continuous_running = true;
    
    // 데이터 처리 태스크 시작
    xTaskCreate(adc_data_process_task, "adc_data_process", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "ADC DMA Continuous Mode started");
    return ESP_OK;
}

// ADC Continuous Mode 정지
esp_err_t adc_dma_continuous_stop(void)
{
    if (adc_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    adc_continuous_running = false;
    
    esp_err_t ret = adc_continuous_stop(adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop ADC continuous: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "ADC DMA Continuous Mode stopped");
    return ESP_OK;
}

// ADC 데이터 가져오기
esp_err_t adc_dma_get_data(uint32_t *channel_0_data, uint32_t *channel_1_data, uint32_t *data_count)
{
    if (xSemaphoreTake(adc_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (adc_data.buffer_full) {
            memcpy(channel_0_data, adc_data.channel_0_data, ADC_BUFFER_SIZE * sizeof(uint32_t));
            memcpy(channel_1_data, adc_data.channel_1_data, ADC_BUFFER_SIZE * sizeof(uint32_t));
            *data_count = ADC_BUFFER_SIZE;
            adc_data.buffer_full = false;
        } else {
            memcpy(channel_0_data, adc_data.channel_0_data, adc_data.buffer_index * sizeof(uint32_t));
            memcpy(channel_1_data, adc_data.channel_1_data, adc_data.buffer_index * sizeof(uint32_t));
            *data_count = adc_data.buffer_index;
        }
        xSemaphoreGive(adc_data_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// ADC 최신 값 가져오기 (캘리브레이션 적용)
esp_err_t adc_dma_get_latest_voltage(uint32_t *voltage_ch0_mv, uint32_t *voltage_ch1_mv)
{
    if (xSemaphoreTake(adc_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (adc_data.buffer_index > 0) {
            uint32_t latest_ch0 = adc_data.channel_0_data[adc_data.buffer_index - 1];
            uint32_t latest_ch1 = adc_data.channel_1_data[adc_data.buffer_index - 1];
            
            // 캘리브레이션 적용
            if (adc1_cali_handle) {
                int voltage0, voltage1;
                adc_cali_raw_to_voltage(adc1_cali_handle, latest_ch0, &voltage0);
                adc_cali_raw_to_voltage(adc1_cali_handle, latest_ch1, &voltage1);
                *voltage_ch0_mv = voltage0;
                *voltage_ch1_mv = voltage1;
            } else {
                // 캘리브레이션 없이 대략적인 변환
                *voltage_ch0_mv = (latest_ch0 * 3300) / 4095;
                *voltage_ch1_mv = (latest_ch1 * 3300) / 4095;
            }
            xSemaphoreGive(adc_data_mutex);
            return ESP_OK;
        }
        xSemaphoreGive(adc_data_mutex);
    }
    
    return ESP_ERR_NOT_FOUND;
}

// ADC 통계 정보 가져오기
esp_err_t adc_dma_get_statistics(uint32_t *min_ch0, uint32_t *max_ch0, uint32_t *avg_ch0,
                                uint32_t *min_ch1, uint32_t *max_ch1, uint32_t *avg_ch1)
{
    if (xSemaphoreTake(adc_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (adc_data.buffer_index > 0) {
            uint32_t min0 = UINT32_MAX, max0 = 0, sum0 = 0;
            uint32_t min1 = UINT32_MAX, max1 = 0, sum1 = 0;
            
            for (int i = 0; i < adc_data.buffer_index; i++) {
                uint32_t val0 = adc_data.channel_0_data[i];
                uint32_t val1 = adc_data.channel_1_data[i];
                
                if (val0 < min0) min0 = val0;
                if (val0 > max0) max0 = val0;
                sum0 += val0;
                
                if (val1 < min1) min1 = val1;
                if (val1 > max1) max1 = val1;
                sum1 += val1;
            }
            
            *min_ch0 = min0;
            *max_ch0 = max0;
            *avg_ch0 = sum0 / adc_data.buffer_index;
            
            *min_ch1 = min1;
            *max_ch1 = max1;
            *avg_ch1 = sum1 / adc_data.buffer_index;
            
            xSemaphoreGive(adc_data_mutex);
            return ESP_OK;
        }
        xSemaphoreGive(adc_data_mutex);
    }
    
    return ESP_ERR_NOT_FOUND;
}

// ADC 정리
void adc_dma_continuous_deinit(void)
{
    if (adc_continuous_running) {
        adc_dma_continuous_stop();
    }
    
    if (adc_handle) {
        adc_continuous_deinit(adc_handle);
        adc_handle = NULL;
    }
    
    if (adc1_cali_handle) {
        adc_cali_delete_scheme_line_fitting(adc1_cali_handle);
        adc1_cali_handle = NULL;
    }
    
    if (adc_data_mutex) {
        vSemaphoreDelete(adc_data_mutex);
        adc_data_mutex = NULL;
    }
    
    if (adc_queue) {
        vQueueDelete(adc_queue);
        adc_queue = NULL;
    }
    
    ESP_LOGI(TAG, "ADC DMA Continuous Mode deinitialized");
}
