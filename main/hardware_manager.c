#include "hardware_manager.h"
#include "hardware_manager_multi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <string.h>

static const char *TAG = "HW_MANAGER";

// 전역 하드웨어 관리자 인스턴스
hw_manager_t g_hw_manager = {0};

// FreeRTOS 객체들
static SemaphoreHandle_t hw_mutex = NULL;
static QueueHandle_t adc_queue = NULL;
static TaskHandle_t continuous_task_handle = NULL;

// ADC 설정 상수
#define ADC_UNIT                    ADC_UNIT_1
#define ADC_CONV_MODE               ADC_CONV_SINGLE_UNIT_1
#define ADC_OUTPUT_TYPE             ADC_DIGI_OUTPUT_FORMAT_TYPE1
#define ADC_BITWIDTH                ADC_BITWIDTH_12

// 감쇠율별 전압 범위 (mV)
static const uint32_t attenuation_voltage_range[HW_ATTEN_MAX] = {
    950,   // HW_ATTEN_0DB
    1250,  // HW_ATTEN_2_5DB
    1750,  // HW_ATTEN_6DB
    3100,  // HW_ATTEN_11DB
};

// ADC Continuous Mode 콜백 함수
static bool IRAM_ATTR s_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)
{
    BaseType_t must_yield = pdFALSE;
    
    if (adc_queue != NULL) {
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

// ADC Continuous 태스크 (내부 함수)
static void adc_continuous_task(void *pvParameters);

// 하드웨어 관리자 초기화
esp_err_t hw_manager_init(void)
{
    esp_err_t ret = ESP_OK;
    
    if (g_hw_manager.initialized) {
        ESP_LOGW(TAG, "Hardware manager already initialized");
        return ESP_OK;
    }
    
    // 뮤텍스와 큐 생성
    hw_mutex = xSemaphoreCreateMutex();
    if (hw_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    adc_queue = xQueueCreate(10, sizeof(adc_continuous_evt_data_t));
    if (adc_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue");
        vSemaphoreDelete(hw_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    // 다중 버퍼 초기화
    g_hw_manager.multi_buffer = malloc(sizeof(multi_buffer_manager_t));
    if (g_hw_manager.multi_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate multi-buffer");
        goto cleanup;
    }
    
    ret = multi_buffer_init((multi_buffer_manager_t *)g_hw_manager.multi_buffer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize multi-buffer: %s", esp_err_to_name(ret));
        free(g_hw_manager.multi_buffer);
        g_hw_manager.multi_buffer = NULL;
        goto cleanup;
    }
    
    // 기본 채널 설정 초기화
    for (int i = 0; i < HW_ADC_CHANNEL_MAX; i++) {
        g_hw_manager.channels[i].channel = (hw_adc_channel_t)i;
        g_hw_manager.channels[i].attenuation = HW_ATTEN_11DB;
        g_hw_manager.channels[i].enabled = false;
        g_hw_manager.channels[i].continuous_mode = (i >= 4); // 채널 4,5는 Continuous 모드
        g_hw_manager.channels[i].sample_rate_hz = 10000;
        g_hw_manager.channels[i].buffer_size = HW_ADC_BUFFER_SIZE;
    }
    
    // TRIG 설정 초기화
    g_hw_manager.trig_config.mode = HW_TRIG_MODE_AUTO;
    g_hw_manager.trig_config.edge = HW_TRIG_EDGE_RISING;
    g_hw_manager.trig_config.level_mv = 1500;
    g_hw_manager.trig_config.pre_trigger_samples = 100;
    g_hw_manager.trig_config.post_trigger_samples = 900;
    g_hw_manager.trig_config.enabled = false;
    
    // GPIO 핀 설정
    g_hw_manager.trig_pin = GPIO_NUM_25; // 기본 TRIG 핀
    g_hw_manager.led_pin = GPIO_NUM_2;   // 기본 LED 핀
    
    // ADC Continuous Mode 설정
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = HW_ADC_BUFFER_SIZE * 4,
        .conv_frame_size = HW_ADC_BUFFER_SIZE,
    };
    ret = adc_continuous_new_handle(&adc_config, &g_hw_manager.continuous_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ADC continuous handle: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // ADC Oneshot Mode 설정
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ret = adc_oneshot_new_unit(&init_config, &g_hw_manager.oneshot_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ADC oneshot unit: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // ADC 캘리브레이션 초기화
    ret = adc_calibration_init(ADC_UNIT, ADC_CHANNEL_0, ADC_ATTEN_DB_12, &g_hw_manager.cali_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ADC calibration failed, continuing without calibration");
    }
    
    // GPIO 설정
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_34) | (1ULL << GPIO_NUM_35) | 
                       (1ULL << GPIO_NUM_36) | (1ULL << GPIO_NUM_37) | 
                       (1ULL << GPIO_NUM_38) | (1ULL << GPIO_NUM_39),
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    // ADC 이벤트 콜백 등록
    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = s_conv_done_cb,
    };
    ret = adc_continuous_register_event_callbacks(g_hw_manager.continuous_handle, &cbs, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register ADC callbacks: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    g_hw_manager.initialized = true;
    g_hw_manager.running = false;
    g_hw_manager.current_mode = HW_ADC_MODE_HYBRID;
    
    ESP_LOGI(TAG, "Hardware manager initialized successfully with multi-buffer (32-bit original data)");
    return ESP_OK;
    
cleanup:
    if (g_hw_manager.continuous_handle) {
        adc_continuous_deinit(g_hw_manager.continuous_handle);
        g_hw_manager.continuous_handle = NULL;
    }
    if (g_hw_manager.oneshot_handle) {
        adc_oneshot_del_unit(g_hw_manager.oneshot_handle);
        g_hw_manager.oneshot_handle = NULL;
    }
    if (g_hw_manager.multi_buffer) {
        multi_buffer_deinit((multi_buffer_manager_t *)g_hw_manager.multi_buffer);
        free(g_hw_manager.multi_buffer);
        g_hw_manager.multi_buffer = NULL;
    }
    if (hw_mutex) {
        vSemaphoreDelete(hw_mutex);
        hw_mutex = NULL;
    }
    if (adc_queue) {
        vQueueDelete(adc_queue);
        adc_queue = NULL;
    }
    return ret;
}

// 하드웨어 관리자 정리
esp_err_t hw_manager_deinit(void)
{
    if (!g_hw_manager.initialized) {
        return ESP_OK;
    }
    
    if (g_hw_manager.running) {
        hw_adc_stop();
    }
    
    if (g_hw_manager.continuous_handle) {
        adc_continuous_deinit(g_hw_manager.continuous_handle);
        g_hw_manager.continuous_handle = NULL;
    }
    
    if (g_hw_manager.oneshot_handle) {
        adc_oneshot_del_unit(g_hw_manager.oneshot_handle);
        g_hw_manager.oneshot_handle = NULL;
    }
    
    if (g_hw_manager.cali_handle) {
        adc_cali_delete_scheme_line_fitting(g_hw_manager.cali_handle);
        g_hw_manager.cali_handle = NULL;
    }
    
    // 다중 버퍼 정리
    if (g_hw_manager.multi_buffer) {
        multi_buffer_deinit((multi_buffer_manager_t *)g_hw_manager.multi_buffer);
        free(g_hw_manager.multi_buffer);
        g_hw_manager.multi_buffer = NULL;
    }
    
    if (hw_mutex) {
        vSemaphoreDelete(hw_mutex);
        hw_mutex = NULL;
    }
    
    if (adc_queue) {
        vQueueDelete(adc_queue);
        adc_queue = NULL;
    }
    
    g_hw_manager.initialized = false;
    ESP_LOGI(TAG, "Hardware manager deinitialized");
    return ESP_OK;
}

// 채널 활성화/비활성화
esp_err_t hw_channel_enable(hw_adc_channel_t channel, bool enable)
{
    if (channel >= HW_ADC_CHANNEL_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(hw_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_hw_manager.channels[channel].enabled = enable;
        xSemaphoreGive(hw_mutex);
        
        ESP_LOGI(TAG, "Channel %s %s", hw_get_channel_name(channel), enable ? "enabled" : "disabled");
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// 채널 감쇠율 설정
esp_err_t hw_channel_set_attenuation(hw_adc_channel_t channel, hw_attenuation_t attenuation)
{
    if (channel >= HW_ADC_CHANNEL_MAX || attenuation >= HW_ATTEN_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(hw_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_hw_manager.channels[channel].attenuation = attenuation;
        xSemaphoreGive(hw_mutex);
        
        ESP_LOGI(TAG, "Channel %s attenuation set to %s", 
                 hw_get_channel_name(channel), hw_get_attenuation_name(attenuation));
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// 채널 샘플링 레이트 설정
esp_err_t hw_channel_set_sample_rate(hw_adc_channel_t channel, uint32_t sample_rate_hz)
{
    if (channel >= HW_ADC_CHANNEL_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(hw_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_hw_manager.channels[channel].sample_rate_hz = sample_rate_hz;
        xSemaphoreGive(hw_mutex);
        
        ESP_LOGI(TAG, "Channel %s sample rate set to %lu Hz", 
                 hw_get_channel_name(channel), sample_rate_hz);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// 채널 버퍼 크기 설정
esp_err_t hw_channel_set_buffer_size(hw_adc_channel_t channel, uint32_t buffer_size)
{
    if (channel >= HW_ADC_CHANNEL_MAX || buffer_size > HW_ADC_BUFFER_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(hw_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_hw_manager.channels[channel].buffer_size = buffer_size;
        xSemaphoreGive(hw_mutex);
        
        ESP_LOGI(TAG, "Channel %s buffer size set to %lu", 
                 hw_get_channel_name(channel), buffer_size);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// TRIG 핀 설정
esp_err_t hw_trig_set_pin(gpio_num_t pin)
{
    if (xSemaphoreTake(hw_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_hw_manager.trig_pin = pin;
        xSemaphoreGive(hw_mutex);
        
        ESP_LOGI(TAG, "TRIG pin set to GPIO%d", pin);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// TRIG 모드 설정
esp_err_t hw_trig_set_mode(hw_trig_mode_t mode)
{
    if (mode >= HW_TRIG_MODE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(hw_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_hw_manager.trig_config.mode = mode;
        xSemaphoreGive(hw_mutex);
        
        ESP_LOGI(TAG, "TRIG mode set to %d", mode);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// TRIG 엣지 설정
esp_err_t hw_trig_set_edge(hw_trig_edge_t edge)
{
    if (edge >= HW_TRIG_EDGE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(hw_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_hw_manager.trig_config.edge = edge;
        xSemaphoreGive(hw_mutex);
        
        ESP_LOGI(TAG, "TRIG edge set to %d", edge);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// TRIG 레벨 설정
esp_err_t hw_trig_set_level(uint32_t level_mv)
{
    if (xSemaphoreTake(hw_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_hw_manager.trig_config.level_mv = level_mv;
        xSemaphoreGive(hw_mutex);
        
        ESP_LOGI(TAG, "TRIG level set to %lu mV", level_mv);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// TRIG Pre/Post 샘플 설정
esp_err_t hw_trig_set_pre_post_samples(uint32_t pre_samples, uint32_t post_samples)
{
    if (xSemaphoreTake(hw_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_hw_manager.trig_config.pre_trigger_samples = pre_samples;
        g_hw_manager.trig_config.post_trigger_samples = post_samples;
        xSemaphoreGive(hw_mutex);
        
        ESP_LOGI(TAG, "TRIG pre/post samples set to %lu/%lu", pre_samples, post_samples);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// TRIG 활성화/비활성화
esp_err_t hw_trig_enable(bool enable)
{
    if (xSemaphoreTake(hw_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_hw_manager.trig_config.enabled = enable;
        xSemaphoreGive(hw_mutex);
        
        ESP_LOGI(TAG, "TRIG %s", enable ? "enabled" : "disabled");
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// ADC 모드 설정
esp_err_t hw_adc_set_mode(hw_adc_mode_t mode)
{
    if (mode >= HW_ADC_MODE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(hw_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_hw_manager.current_mode = mode;
        xSemaphoreGive(hw_mutex);
        
        ESP_LOGI(TAG, "ADC mode set to %d", mode);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// ADC 시작
esp_err_t hw_adc_start(void)
{
    if (!g_hw_manager.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (g_hw_manager.running) {
        ESP_LOGW(TAG, "ADC already running");
        return ESP_OK;
    }
    
    // Continuous 모드 태스크 시작
    BaseType_t ret = xTaskCreate(adc_continuous_task, "adc_continuous", 4096, NULL, 8, &continuous_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ADC continuous task");
        return ESP_ERR_NO_MEM;
    }
    
    g_hw_manager.running = true;
    ESP_LOGI(TAG, "ADC started with multi-buffer");
    return ESP_OK;
}

// ADC 정지
esp_err_t hw_adc_stop(void)
{
    if (!g_hw_manager.running) {
        return ESP_OK;
    }
    
    g_hw_manager.running = false;
    
    // 태스크 종료 대기
    if (continuous_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(100));
        continuous_task_handle = NULL;
    }
    
    ESP_LOGI(TAG, "ADC stopped");
    return ESP_OK;
}

// 32-bit 원본 데이터 가져오기
esp_err_t hw_get_channel_data_32bit(hw_adc_channel_t channel, uint32_t *raw_data, uint64_t *timestamps, uint32_t *data_count)
{
    if (channel >= HW_ADC_CHANNEL_MAX || !raw_data || !timestamps || !data_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 다중 버퍼에서 32-bit 원본 데이터 가져오기
    return multi_buffer_get_ready_data((multi_buffer_manager_t *)g_hw_manager.multi_buffer, raw_data, timestamps, data_count);
}

// 16-bit 표시용 데이터 가져오기
esp_err_t hw_get_channel_data_16bit(hw_adc_channel_t channel, uint16_t *display_data, uint32_t *data_count)
{
    if (channel >= HW_ADC_CHANNEL_MAX || !display_data || !data_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 다중 버퍼에서 16-bit 표시용 데이터 가져오기
    return multi_buffer_get_16bit_display_data((multi_buffer_manager_t *)g_hw_manager.multi_buffer, display_data, data_count);
}

// 최신 전압 값 가져오기
esp_err_t hw_get_latest_voltage(hw_adc_channel_t channel, uint32_t *voltage_mv)
{
    if (channel >= HW_ADC_CHANNEL_MAX || !voltage_mv) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 다중 버퍼에서 최신 32-bit 데이터 가져오기
    uint32_t temp_data[MULTI_BUFFER_SIZE];
    uint64_t temp_timestamps[MULTI_BUFFER_SIZE];
    uint32_t temp_count;
    
    esp_err_t ret = multi_buffer_get_ready_data((multi_buffer_manager_t *)g_hw_manager.multi_buffer, temp_data, temp_timestamps, &temp_count);
    if (ret == ESP_OK && temp_count > 0) {
        // 최신 값을 전압으로 변환
        uint32_t latest_raw = temp_data[temp_count - 1];
        *voltage_mv = hw_raw_to_voltage(latest_raw, g_hw_manager.channels[channel].attenuation);
        return ESP_OK;
    }
    
    return ESP_ERR_NOT_FOUND;
}

// TRIG 이벤트 가져오기
esp_err_t hw_get_trig_event(hw_trig_event_t *trig_event)
{
    if (!trig_event) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(hw_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        *trig_event = g_hw_manager.trig_event;
        xSemaphoreGive(hw_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// 16-bit 트리거 데이터 가져오기
esp_err_t hw_get_trigger_data_16bit(uint16_t *pre_data, uint16_t *post_data, uint64_t *pre_timestamps, uint64_t *post_timestamps)
{
    if (!pre_data || !post_data || !pre_timestamps || !post_timestamps) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 임시 32-bit 버퍼
    uint32_t temp_pre_data[MULTI_TRIG_PRE_SAMPLES];
    uint32_t temp_post_data[MULTI_TRIG_POST_SAMPLES];
    uint64_t temp_pre_timestamps[MULTI_TRIG_PRE_SAMPLES];
    uint64_t temp_post_timestamps[MULTI_TRIG_POST_SAMPLES];
    
    // 32-bit 트리거 데이터 가져오기
    esp_err_t ret = multi_buffer_capture_trigger_data((multi_buffer_manager_t *)g_hw_manager.multi_buffer, 
                                                     temp_pre_data, temp_post_data,
                                                     temp_pre_timestamps, temp_post_timestamps);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 32-bit를 16-bit로 변환
    ret = multi_buffer_convert_to_16bit(temp_pre_data, pre_data, MULTI_TRIG_PRE_SAMPLES);
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = multi_buffer_convert_to_16bit(temp_post_data, post_data, MULTI_TRIG_POST_SAMPLES);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 타임스탬프 복사
    memcpy(pre_timestamps, temp_pre_timestamps, MULTI_TRIG_PRE_SAMPLES * sizeof(uint64_t));
    memcpy(post_timestamps, temp_post_timestamps, MULTI_TRIG_POST_SAMPLES * sizeof(uint64_t));
    
    return ESP_OK;
}

// 채널 통계 정보 가져오기
esp_err_t hw_get_channel_statistics(hw_adc_channel_t channel, uint32_t *min_val, uint32_t *max_val, uint32_t *avg_val)
{
    if (channel >= HW_ADC_CHANNEL_MAX || !min_val || !max_val || !avg_val) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 다중 버퍼에서 32-bit 데이터 가져오기
    uint32_t temp_data[MULTI_BUFFER_SIZE];
    uint64_t temp_timestamps[MULTI_BUFFER_SIZE];
    uint32_t temp_count;
    
    esp_err_t ret = multi_buffer_get_ready_data((multi_buffer_manager_t *)g_hw_manager.multi_buffer, temp_data, temp_timestamps, &temp_count);
    if (ret != ESP_OK || temp_count == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // 통계 계산
    uint32_t min_v = UINT32_MAX, max_v = 0, sum_v = 0;
    
    for (uint32_t i = 0; i < temp_count; i++) {
        uint32_t val = temp_data[i];
        if (val < min_v) min_v = val;
        if (val > max_v) max_v = val;
        sum_v += val;
    }
    
    *min_val = min_v;
    *max_val = max_v;
    *avg_val = sum_v / temp_count;
    
    return ESP_OK;
}

// Raw 값을 전압으로 변환
uint32_t hw_raw_to_voltage(uint32_t raw_value, hw_attenuation_t attenuation)
{
    if (attenuation >= HW_ATTEN_MAX) {
        return 0;
    }
    
    // 12비트 ADC (0-4095)를 전압으로 변환
    uint32_t voltage_range = attenuation_voltage_range[attenuation];
    return (raw_value * voltage_range) / 4095;
}

// 전압을 Raw 값으로 변환
uint32_t hw_voltage_to_raw(uint32_t voltage_mv, hw_attenuation_t attenuation)
{
    if (attenuation >= HW_ATTEN_MAX) {
        return 0;
    }
    
    // 전압을 12비트 ADC 값으로 변환
    uint32_t voltage_range = attenuation_voltage_range[attenuation];
    return (voltage_mv * 4095) / voltage_range;
}

// 채널 이름 가져오기
const char* hw_get_channel_name(hw_adc_channel_t channel)
{
    static const char* channel_names[HW_ADC_CHANNEL_MAX] = {
        "CH0 (GPIO36)",
        "CH1 (GPIO37)",
        "CH2 (GPIO38)",
        "CH3 (GPIO39)",
        "CH4 (GPIO34)",
        "CH5 (GPIO35)",
        "CH6 (GPIO32)",
        "CH7 (GPIO33)",
    };
    
    if (channel >= HW_ADC_CHANNEL_MAX) {
        return "Unknown";
    }
    
    return channel_names[channel];
}

// 감쇠율 이름 가져오기
const char* hw_get_attenuation_name(hw_attenuation_t attenuation)
{
    static const char* attenuation_names[HW_ATTEN_MAX] = {
        "0dB (0-950mV)",
        "2.5dB (0-1250mV)",
        "6dB (0-1750mV)",
        "11dB (0-3100mV)",
    };
    
    if (attenuation >= HW_ATTEN_MAX) {
        return "Unknown";
    }
    
    return attenuation_names[attenuation];
}

// ADC Continuous 태스크 (내부 함수)
static void adc_continuous_task(void *pvParameters)
{
    adc_continuous_evt_data_t evt_data;
    static uint32_t processed_count = 0;
    
    ESP_LOGI(TAG, "ADC continuous task started (Core %d)", xPortGetCoreID());
    
    // ADC Continuous 모드 시작
    esp_err_t ret = adc_continuous_start(g_hw_manager.continuous_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start ADC continuous: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
    
    while (g_hw_manager.running) {
        if (adc_queue != NULL) {
            if (xQueueReceive(adc_queue, &evt_data, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (evt_data.conv_frame_buffer != NULL && evt_data.size > 0) {
                    // ADC 데이터 처리 (32-bit 원본 데이터로 저장)
                    uint16_t *data = (uint16_t *)evt_data.conv_frame_buffer;
                    uint32_t samples = evt_data.size / sizeof(uint16_t);
                    
                    if (samples > MULTI_BUFFER_SIZE * 2) {
                        samples = MULTI_BUFFER_SIZE * 2;
                    }
                    
                    // 채널별로 데이터 분리 (Continuous 모드: CH4, CH5)
                    for (int i = 0; i < samples; i += 2) {
                        if (i + 1 < samples) {
                            uint16_t raw0 = data[i] & 0xFFF;
                            uint16_t raw1 = data[i + 1] & 0xFFF;
                            
                            // 32-bit 원본 데이터로 저장 (가공 없이)
                            uint32_t timestamp = esp_timer_get_time();
                            
                            // CH4 (GPIO34) 데이터 저장
                            if (g_hw_manager.channels[HW_ADC_CHANNEL_4].enabled) {
                                multi_buffer_write((multi_buffer_manager_t *)g_hw_manager.multi_buffer, (uint32_t)raw0, timestamp);
                            }
                            
                            // CH5 (GPIO35) 데이터 저장
                            if (g_hw_manager.channels[HW_ADC_CHANNEL_5].enabled) {
                                multi_buffer_write((multi_buffer_manager_t *)g_hw_manager.multi_buffer, (uint32_t)raw1, timestamp);
                            }
                        }
                    }
                    
                    processed_count++;
                    
                    // 트리거 감지 (필요시)
                    if (g_hw_manager.trig_config.enabled) {
                        multi_buffer_check_trigger((multi_buffer_manager_t *)g_hw_manager.multi_buffer, 
                                                 g_hw_manager.trig_config.level_mv,
                                                 g_hw_manager.trig_config.edge);
                    }
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    
    // ADC Continuous 모드 정지
    adc_continuous_stop(g_hw_manager.continuous_handle);
    ESP_LOGI(TAG, "ADC continuous task ended (Core %d)", xPortGetCoreID());
    vTaskDelete(NULL);
}
