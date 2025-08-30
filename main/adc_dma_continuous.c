#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "adc_dma_continuous.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"

static const char *TAG = "ADC_CONTINUOUS";

// ADC 설정
#define ADC_UNIT                    ADC_UNIT_1
#define ADC_CONV_MODE               ADC_CONV_SINGLE_UNIT_1
#define ADC_OUTPUT_TYPE             ADC_DIGI_OUTPUT_FORMAT_TYPE1
#define ADC_ATTEN                   ADC_ATTEN_DB_12  // 0~3.9V 범위
#define ADC_BITWIDTH                ADC_BITWIDTH_12   // 12비트

// ADC 채널 설정 (ADC1만 사용 - WiFi 충돌 방지)
#define ADC_CONTINUOUS_CHANNEL_NUM  2
#define ADC_CONTINUOUS_CHANNEL_0    ADC_CHANNEL_6  // GPIO34
#define ADC_CONTINUOUS_CHANNEL_1    ADC_CHANNEL_7  // GPIO35

// ADC Polling 채널 설정 (ADC1 사용)
#define ADC_POLLING_CHANNEL_0       ADC_CHANNEL_0  // GPIO36 (ADC1_CH0)
#define ADC_POLLING_CHANNEL_1       ADC_CHANNEL_1  // GPIO37 (ADC1_CH1)
#define ADC_POLLING_CHANNEL_2       ADC_CHANNEL_2  // GPIO38 (ADC1_CH2)
#define ADC_POLLING_CHANNEL_3       ADC_CHANNEL_3  // GPIO39 (ADC1_CH3)


// ADC 결과 구조체
typedef struct {
    uint32_t continuous_ch0_data[ADC_BUFFER_SIZE];  // GPIO34 (Continuous)
    uint32_t continuous_ch1_data[ADC_BUFFER_SIZE];  // GPIO35 (Continuous)
    uint32_t polling_ch0_data[ADC_POLLING_BUFFER_SIZE];     // GPIO32 (Polling)
    uint32_t polling_ch1_data[ADC_POLLING_BUFFER_SIZE];     // GPIO33 (Polling)
    uint32_t polling_ch2_data[ADC_POLLING_BUFFER_SIZE];     // GPIO33 (Polling)
    uint32_t polling_ch3_data[ADC_POLLING_BUFFER_SIZE];     // GPIO33 (Polling)
    uint32_t buffer_index;
    bool buffer_full;
    int cnt;
    uint32_t polling_index;
} adc_data_t;

// EventGroup 비트 정의
#define ADC_CONTINUOUS_RUNNING_BIT    BIT0
#define ADC_POLLING_REQUEST_BIT       BIT1

// 전역 변수
adc_continuous_handle_t adc_continuous_handle = NULL;
adc_oneshot_unit_handle_t adc_oneshot_handle = NULL;
adc_cali_handle_t adc1_cali_handle = NULL;
adc_data_t adc_data = {0};
SemaphoreHandle_t adc_data_mutex = NULL;
QueueHandle_t adc_queue = NULL;
bool adc_continuous_running = false;
TaskHandle_t polling_task_handle = NULL;
EventGroupHandle_t adc_event_group = NULL;

// ADC Continuous Mode 콜백 함수
static bool IRAM_ATTR s_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)
{
    BaseType_t must_yield = pdFALSE;
    
    // 큐가 NULL이 아닌지 확인
    if (adc_queue != NULL) {
        // ADC 데이터를 큐에 전송
        xQueueSendFromISR(adc_queue, edata, &must_yield);
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

// ADC 초기화 (Continuous + Polling 모드)
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
    
    // ADC Continuous Mode 설정 (GPIO34, GPIO35)
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = ADC_BUFFER_SIZE * 4,  // 8 → 4로 줄임
        .conv_frame_size = ADC_BUFFER_SIZE,
    };
    ret = adc_continuous_new_handle(&adc_config, &adc_continuous_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ADC continuous handle: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // ADC Continuous 채널 설정 (GPIO34, GPIO35)
    adc_digi_pattern_config_t adc_pattern[ADC_CONTINUOUS_CHANNEL_NUM] = {
        {
            .atten = ADC_ATTEN,
            .channel = ADC_CONTINUOUS_CHANNEL_0,  // GPIO34
            .unit = ADC_UNIT,
            .bit_width = ADC_BITWIDTH,
        },
        {
            .atten = ADC_ATTEN,
            .channel = ADC_CONTINUOUS_CHANNEL_1,  // GPIO35
            .unit = ADC_UNIT,
            .bit_width = ADC_BITWIDTH,
        },
    };
    
    adc_continuous_config_t dig_cfg = {
        .pattern_num = ADC_CONTINUOUS_CHANNEL_NUM,
        .adc_pattern = adc_pattern,
        .sample_freq_hz = ADC_SAMPLE_FREQ_HZ,
        .conv_mode = ADC_CONV_MODE,
        .format = ADC_OUTPUT_TYPE,
    };
    
    ret = adc_continuous_config(adc_continuous_handle, &dig_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC continuous: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // ADC Oneshot Mode 설정 (GPIO36-39)
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ret = adc_oneshot_new_unit(&init_config1, &adc_oneshot_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ADC oneshot unit: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // ADC Oneshot 채널 설정 (GPIO36-39)
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH,
        .atten = ADC_ATTEN,
    };
    
    ret = adc_oneshot_config_channel(adc_oneshot_handle, ADC_POLLING_CHANNEL_0, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC polling channel 0: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ret = adc_oneshot_config_channel(adc_oneshot_handle, ADC_POLLING_CHANNEL_1, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC polling channel 1: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ret = adc_oneshot_config_channel(adc_oneshot_handle, ADC_POLLING_CHANNEL_2, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC polling channel 2: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ret = adc_oneshot_config_channel(adc_oneshot_handle, ADC_POLLING_CHANNEL_3, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC polling channel 3: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    

    // ADC 캘리브레이션 초기화
    ret = adc_calibration_init(ADC_UNIT, ADC_CONTINUOUS_CHANNEL_0, ADC_ATTEN, &adc1_cali_handle);
    if (ret != ESP_OK) {    
        ESP_LOGW(TAG, "ADC calibration failed, continuing without calibration");
    }
    
    // GPIO34, GPIO35를 아날로그 입력으로 설정 (Continuous 모드)
    gpio_config_t io_conf_continuous = {
        .pin_bit_mask = (1ULL << GPIO_NUM_34) | (1ULL << GPIO_NUM_35),
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf_continuous);
    
    // GPIO36-39를 아날로그 입력으로 설정 (Polling 모드)
    gpio_config_t io_conf_polling = {
        .pin_bit_mask = (1ULL << GPIO_NUM_36) | (1ULL << GPIO_NUM_37) | (1ULL << GPIO_NUM_38) | (1ULL << GPIO_NUM_39),
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf_polling);
    
    // ADC 이벤트 콜백 등록
    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = s_conv_done_cb,
    };
    ret = adc_continuous_register_event_callbacks(adc_continuous_handle, &cbs, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register ADC callbacks: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ESP_LOGI(TAG, "GPIO34, GPIO35 configured for continuous mode");
    ESP_LOGI(TAG, "GPIO36-39 configured for polling mode (with continuous pause)");
    ESP_LOGI(TAG, "ADC Continuous + Polling Mode initialized successfully");
    return ESP_OK;
    
cleanup:
    if (adc_continuous_handle) {
        adc_continuous_deinit(adc_continuous_handle);
        adc_continuous_handle = NULL;
    }
    if (adc_oneshot_handle) {
        adc_oneshot_del_unit(adc_oneshot_handle);
        adc_oneshot_handle = NULL;
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

// ADC Continuous 모드 관리 태스크 (GitHub 이슈 해결책 적용)
static void adc_continuous_manager_task(void *pvParameters)
{
    int adc_raw0, adc_raw1, adc_raw2, adc_raw3;
    static uint32_t polling_counter = 0;
    bool continuous_running = true;
    
    ESP_LOGI(TAG, "ADC continuous manager task started (Core %d)", xPortGetCoreID());
    
    // ADC Continuous 모드 시작 (같은 태스크에서 시작)
    esp_err_t ret_start = adc_continuous_start(adc_continuous_handle);
    if (ret_start != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start ADC continuous: %s (Core %d)", esp_err_to_name(ret_start), xPortGetCoreID());
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "ADC continuous mode started (Core %d)", xPortGetCoreID());
    
    while (adc_continuous_running) {
        polling_counter++;
        
        // Polling 주기에 도달하면 Continuous 모드를 잠깐 멈추고 Polling 수행
        if (polling_counter >= (ADC_POLLING_INTERVAL_SEC * 10)) { // 10Hz 기준으로 계산
            //ESP_LOGI(TAG, "Starting ADC polling cycle (same task)... (Core %d)", xPortGetCoreID());
            
            // 1. Continuous 모드 일시 중지 (같은 태스크에서 중지)
            //ESP_LOGI(TAG, "adc_continuous_stop (same task)");
            esp_err_t ret_stop = adc_continuous_stop(adc_continuous_handle);
            if (ret_stop != ESP_OK) {
                ESP_LOGE(TAG, "Failed to stop ADC continuous: %s (Core %d)", esp_err_to_name(ret_stop), xPortGetCoreID());
            } else {
                //ESP_LOGI(TAG, "ADC continuous mode paused for polling (Core %d)", xPortGetCoreID());
                
                // 2. 잠깐 대기 (ADC 안정화)
                vTaskDelay(pdMS_TO_TICKS(10));
                
                // 3. Polling 수행 (GPIO36-39)
                //ESP_LOGI(TAG, "adc_oneshot_read");
                esp_err_t ret0 = adc_oneshot_read(adc_oneshot_handle, ADC_POLLING_CHANNEL_0, &adc_raw0);
                esp_err_t ret1 = adc_oneshot_read(adc_oneshot_handle, ADC_POLLING_CHANNEL_1, &adc_raw1);
                esp_err_t ret2 = adc_oneshot_read(adc_oneshot_handle, ADC_POLLING_CHANNEL_2, &adc_raw2);
                esp_err_t ret3 = adc_oneshot_read(adc_oneshot_handle, ADC_POLLING_CHANNEL_3, &adc_raw3);
                
                if (ret0 == ESP_OK && ret1 == ESP_OK && ret2 == ESP_OK && ret3 == ESP_OK) {
                    if (xSemaphoreTake(adc_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                        // Polling 데이터 저장 (GPIO36-39)
                        int focus = adc_data.polling_index;

                        adc_data.polling_ch0_data[focus] = (uint32_t)adc_raw0;
                        adc_data.polling_ch1_data[focus] = (uint32_t)adc_raw1;
                        adc_data.polling_ch2_data[focus] = (uint32_t)adc_raw2;
                        adc_data.polling_ch3_data[focus] = (uint32_t)adc_raw3;
                        
                        //ESP_LOGI(TAG, "ADC polling success: ch0=%d, ch1=%d, ch2=%d, ch3=%d (Core %d)", 
                        //         adc_raw0, adc_raw1, adc_raw2, adc_raw3, xPortGetCoreID());

                        if(++(adc_data.polling_index) >= ADC_POLLING_BUFFER_SIZE)
                            adc_data.polling_index = 0;

                        xSemaphoreGive(adc_data_mutex);
                    }
                } else {
                    ESP_LOGE(TAG, "ADC polling read failed: ch0=%s, ch1=%s, ch2=%s, ch3=%s (Core %d)", 
                             esp_err_to_name(ret0), esp_err_to_name(ret1), 
                             esp_err_to_name(ret2), esp_err_to_name(ret3), xPortGetCoreID());
                }
                
                // 4. 잠깐 대기 (ADC 안정화)
                vTaskDelay(pdMS_TO_TICKS(10));
                
                // 5. Continuous 모드 재시작 (같은 태스크에서 재시작)
                //ESP_LOGI(TAG, "adc_continuous_start (same task)");
                ret_start = adc_continuous_start(adc_continuous_handle);
                if (ret_start != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to restart ADC continuous: %s (Core %d)", esp_err_to_name(ret_start), xPortGetCoreID());
                } else {
                    //ESP_LOGI(TAG, "ADC continuous mode resumed (Core %d)", xPortGetCoreID());
                }
            }
            
            // 카운터 리셋
            polling_counter = 0;
        }
        
        // 일반적인 폴링 간격
        vTaskDelay(pdMS_TO_TICKS(100));  // 10Hz 업데이트
    }
    
    // 태스크 종료 시 Continuous 모드 중지 (같은 태스크에서 중지)
    ESP_LOGI(TAG, "Stopping ADC continuous mode (same task)");
    esp_err_t ret_stop = adc_continuous_stop(adc_continuous_handle);
    if (ret_stop != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop ADC continuous: %s (Core %d)", esp_err_to_name(ret_stop), xPortGetCoreID());
    }
    
    ESP_LOGI(TAG, "ADC continuous manager task ended (Core %d)", xPortGetCoreID());
    vTaskDelete(NULL);
}

// ADC 데이터 처리 태스크 (Continuous 모드)
static void adc_data_process_task(void *pvParameters)
{
    adc_continuous_evt_data_t evt_data;
    static uint32_t processed_count = 0;
    
    ESP_LOGI(TAG, "ADC continuous data processing task started (Core %d)", xPortGetCoreID());
    
    while (adc_continuous_running) {
        // 큐가 NULL이 아닌지 확인하고 안전하게 데이터 수신
        if (adc_queue != NULL) {
            // 큐 수신 타임아웃을 늘리고 안전성 강화
            if (xQueueReceive(adc_queue, &evt_data, pdMS_TO_TICKS(100)) == pdTRUE) {
                // 데이터 유효성 검사
                if (evt_data.conv_frame_buffer != NULL && evt_data.size > 0) {
                    if (xSemaphoreTake(adc_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        // ESP32 ADC continuous mode 데이터 처리
                        uint16_t *data = (uint16_t *)evt_data.conv_frame_buffer;
                        uint32_t samples = evt_data.size / sizeof(uint16_t);
                        
                        // 샘플 수 제한 (버퍼 오버플로우 방지)
                        if (samples > ADC_BUFFER_SIZE * 2) {
                            samples = ADC_BUFFER_SIZE * 2;
                        }
                        // 채널별로 데이터 분리 (교대로 들어옴)
                        //if(!adc_data.buffer_full)
                        for (int i = 0; i < samples; i += 2) {
                            if (i + 1 < samples) {
                                // 버퍼를 계속 업데이트
                                if (adc_data.buffer_index >= ADC_BUFFER_SIZE) {
                                    adc_data.buffer_full = true;
                                    break;
                                }
                                uint16_t raw0 = data[i];
                                uint16_t raw1 = data[i + 1];
                                
                                // ESP32 ADC continuous mode에서는 상위 4비트가 채널 정보, 하위 12비트가 ADC 값
                                uint32_t adc_val0 = raw0 & 0xFFF;  // 12비트 ADC 값
                                uint32_t adc_val1 = raw1 & 0xFFF;  // 12비트 ADC 값
                                
                                // Continuous 데이터 저장 (GPIO34, GPIO35)
                                adc_data.continuous_ch0_data[adc_data.buffer_index] = adc_val0;
                                adc_data.continuous_ch1_data[adc_data.buffer_index] = adc_val1;
                                adc_data.buffer_index++;
                                
                            }
                        }
                        processed_count++;
                        
                        // 주기적으로 처리 상태 로그 (Core 정보 포함)
                        if (processed_count % 100 == 0) {
                            //ESP_LOGI(TAG, "Processed %lu continuous ADC frames (Core %d)", processed_count, xPortGetCoreID());
                        }
                        
                        xSemaphoreGive(adc_data_mutex);
                    }
                }
            }
        } else {
            // 큐가 NULL인 경우 잠시 대기
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    
    ESP_LOGI(TAG, "ADC continuous data processing task ended (Core %d)", xPortGetCoreID());
    vTaskDelete(NULL);
}

// ADC 시작
esp_err_t adc_dma_continuous_start(void)
{
    if (adc_continuous_handle == NULL || adc_oneshot_handle == NULL) {
        ESP_LOGE(TAG, "ADC handles not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    BaseType_t ret_task = xTaskCreate(adc_continuous_manager_task, "adc_continuous_manager", 4096, NULL, 8, &polling_task_handle);
    if (ret_task != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ADC polling task");
        adc_continuous_running = false;
        adc_continuous_stop(adc_continuous_handle);
        return ESP_ERR_NO_MEM;
    }
    
    adc_continuous_running = true;
    
    // 데이터 처리 태스크 시작 (자유 할당)
    xTaskCreate(adc_data_process_task, "adc_continuous_process", 4096, NULL, 8, NULL);
    
    
    ESP_LOGI(TAG, "ADC Continuous + Polling Mode started");
    return ESP_OK;
}

// ADC 정지
esp_err_t adc_dma_continuous_stop(void)
{
    if (adc_continuous_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    adc_continuous_running = false;
    
    // 태스크 종료 대기
    if (polling_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(100));
        polling_task_handle = NULL;
    }
    
    esp_err_t ret = adc_continuous_stop(adc_continuous_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop ADC continuous: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "ADC Continuous + Polling Mode stopped");
    return ESP_OK;
}

// ADC 데이터 가져오기 (호환성을 위해 기존 API 유지)
esp_err_t adc_dma_get_data(uint32_t *channel_0_data, uint32_t *channel_1_data, uint32_t *data_count)
{
    if (xSemaphoreTake(adc_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (adc_data.buffer_full/*adc_data.buffer_index > 0*/) {
            uint32_t copy_count = adc_data.buffer_full ? ADC_BUFFER_SIZE : adc_data.buffer_index;
            // Continuous 데이터 반환 (GPIO34, GPIO35)
            memcpy(channel_0_data, adc_data.continuous_ch0_data, copy_count * sizeof(uint32_t));
            memcpy(channel_1_data, adc_data.continuous_ch1_data, copy_count * sizeof(uint32_t));

            if(adc_data.buffer_full){
                adc_data.buffer_index = 0;
                adc_data.buffer_full = false;
            }
            *data_count = copy_count;
            xSemaphoreGive(adc_data_mutex);
            return ESP_OK;
        }
        xSemaphoreGive(adc_data_mutex);
    }
    
    return ESP_ERR_NOT_FOUND;
}

// ADC 최신 값 가져오기 (캘리브레이션 적용)
esp_err_t adc_dma_get_latest_voltage(uint32_t *voltage_ch0_mv, uint32_t *voltage_ch1_mv)
{
    if (xSemaphoreTake(adc_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (adc_data.buffer_index > 0) {
            uint32_t latest_ch0 = adc_data.continuous_ch0_data[adc_data.buffer_index - 1];
            uint32_t latest_ch1 = adc_data.continuous_ch1_data[adc_data.buffer_index - 1];
            
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
                uint32_t val0 = adc_data.continuous_ch0_data[i];
                uint32_t val1 = adc_data.continuous_ch1_data[i];
                
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

// 새로운 함수: Polling 데이터 가져오기 (GPIO36-39)
esp_err_t adc_get_polling_data(uint32_t *ch0_data, uint32_t *ch1_data, uint32_t *ch2_data, uint32_t *ch3_data, uint32_t *data_count)
{
    if (xSemaphoreTake(adc_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        static uint32_t prev_polling_index = 0;
        {
            uint32_t copy_count = adc_data.polling_index - prev_polling_index;
            if (adc_data.polling_index < prev_polling_index)
                copy_count = ADC_POLLING_BUFFER_SIZE - prev_polling_index;
                
            memcpy(ch0_data+prev_polling_index, adc_data.polling_ch0_data+prev_polling_index, copy_count * sizeof(uint32_t));
            memcpy(ch1_data+prev_polling_index, adc_data.polling_ch1_data+prev_polling_index, copy_count * sizeof(uint32_t));
            memcpy(ch2_data+prev_polling_index, adc_data.polling_ch2_data+prev_polling_index, copy_count * sizeof(uint32_t));
            memcpy(ch3_data+prev_polling_index, adc_data.polling_ch3_data+prev_polling_index, copy_count * sizeof(uint32_t));

            if (adc_data.polling_index < prev_polling_index) {
                copy_count = adc_data.polling_index;
                memcpy(ch0_data, adc_data.polling_ch0_data, copy_count * sizeof(uint32_t));
                memcpy(ch1_data, adc_data.polling_ch1_data, copy_count * sizeof(uint32_t));
                memcpy(ch2_data, adc_data.polling_ch2_data, copy_count * sizeof(uint32_t));
                memcpy(ch3_data, adc_data.polling_ch3_data, copy_count * sizeof(uint32_t));
            }

            prev_polling_index = adc_data.polling_index;

            *data_count = copy_count;
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
    
    if (adc_continuous_handle) {
        adc_continuous_deinit(adc_continuous_handle);
        adc_continuous_handle = NULL;
    }
    
    if (adc_oneshot_handle) {
        adc_oneshot_del_unit(adc_oneshot_handle);
        adc_oneshot_handle = NULL;
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
    
    ESP_LOGI(TAG, "ADC Continuous + Polling Mode deinitialized");
}
