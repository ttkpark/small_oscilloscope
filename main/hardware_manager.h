#ifndef HARDWARE_MANAGER_H
#define HARDWARE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"

#ifdef __cplusplus
extern "C" {
#endif

// 하드웨어 설정 상수
#define HW_MAX_ADC_CHANNELS          8
#define HW_MAX_ATTENUATION_LEVELS    4
#define HW_TRIG_BUFFER_SIZE          1024
#define HW_ADC_BUFFER_SIZE           2048
#define HW_POLLING_BUFFER_SIZE       100

// ADC 채널 정의
typedef enum {
    HW_ADC_CHANNEL_0 = 0,  // GPIO36 (ADC1_CH0)
    HW_ADC_CHANNEL_1,      // GPIO37 (ADC1_CH1) 
    HW_ADC_CHANNEL_2,      // GPIO38 (ADC1_CH2)
    HW_ADC_CHANNEL_3,      // GPIO39 (ADC1_CH3)
    HW_ADC_CHANNEL_4,      // GPIO34 (ADC1_CH6) - Continuous
    HW_ADC_CHANNEL_5,      // GPIO35 (ADC1_CH7) - Continuous
    HW_ADC_CHANNEL_6,      // GPIO32 (ADC1_CH4) - Reserved
    HW_ADC_CHANNEL_7,      // GPIO33 (ADC1_CH5) - Reserved
    HW_ADC_CHANNEL_MAX
} hw_adc_channel_t;

// 감쇠율 설정
typedef enum {
    HW_ATTEN_0DB = 0,      // 0~950mV
    HW_ATTEN_2_5DB,        // 0~1250mV
    HW_ATTEN_6DB,          // 0~1750mV
    HW_ATTEN_11DB,         // 0~3100mV
    HW_ATTEN_MAX
} hw_attenuation_t;

// TRIG 모드 설정
typedef enum {
    HW_TRIG_MODE_AUTO = 0,     // 자동 트리거
    HW_TRIG_MODE_NORMAL,       // 일반 트리거
    HW_TRIG_MODE_SINGLE,       // 단일 트리거
    HW_TRIG_MODE_MAX
} hw_trig_mode_t;

// TRIG 엣지 설정
typedef enum {
    HW_TRIG_EDGE_RISING = 0,   // 상승 엣지
    HW_TRIG_EDGE_FALLING,      // 하강 엣지
    HW_TRIG_EDGE_BOTH,         // 양쪽 엣지
    HW_TRIG_EDGE_MAX
} hw_trig_edge_t;

// ADC 모드 설정
typedef enum {
    HW_ADC_MODE_CONTINUOUS = 0,    // 연속 모드
    HW_ADC_MODE_POLLING,           // 폴링 모드
    HW_ADC_MODE_HYBRID,            // 하이브리드 모드 (Continuous + Polling)
    HW_ADC_MODE_MAX
} hw_adc_mode_t;

// 채널 설정 구조체
typedef struct {
    hw_adc_channel_t channel;
    hw_attenuation_t attenuation;
    bool enabled;
    bool continuous_mode;
    uint32_t sample_rate_hz;
    uint32_t buffer_size;
} hw_channel_config_t;

// TRIG 설정 구조체
typedef struct {
    hw_trig_mode_t mode;
    hw_trig_edge_t edge;
    uint32_t level_mv;
    uint32_t pre_trigger_samples;
    uint32_t post_trigger_samples;
    bool enabled;
} hw_trig_config_t;

// ADC 데이터 구조체 (32-bit 원본 데이터)
typedef struct {
    uint32_t raw_data[HW_ADC_BUFFER_SIZE];  // 32-bit 원본 데이터
    uint64_t timestamps[HW_ADC_BUFFER_SIZE];
    uint32_t buffer_index;
    bool buffer_full;
    uint64_t timestamp;
} hw_adc_data_t;

// TRIG 이벤트 구조체
typedef struct {
    uint64_t trigger_time;
    uint32_t trigger_level;
    hw_trig_edge_t trigger_edge;
    bool triggered;
} hw_trig_event_t;

// 하드웨어 관리자 구조체
typedef struct {
    // ADC 핸들들
    adc_continuous_handle_t continuous_handle;
    adc_oneshot_unit_handle_t oneshot_handle;
    adc_cali_handle_t cali_handle;
    
    // 채널 설정
    hw_channel_config_t channels[HW_ADC_CHANNEL_MAX];
    
    // TRIG 설정
    hw_trig_config_t trig_config;
    hw_trig_event_t trig_event;
    
    // 다중 버퍼 관리자 (32-bit 원본 데이터) - 포인터로 선언
    void *multi_buffer;
    
    // 상태 플래그
    bool initialized;
    bool running;
    hw_adc_mode_t current_mode;
    
    // GPIO 핀 설정
    gpio_num_t trig_pin;
    gpio_num_t led_pin;
    
} hw_manager_t;

// 전역 하드웨어 관리자 인스턴스
extern hw_manager_t g_hw_manager;

// 초기화 함수
esp_err_t hw_manager_init(void);
esp_err_t hw_manager_deinit(void);

// 채널 관리 함수
esp_err_t hw_channel_enable(hw_adc_channel_t channel, bool enable);
esp_err_t hw_channel_set_attenuation(hw_adc_channel_t channel, hw_attenuation_t attenuation);
esp_err_t hw_channel_set_sample_rate(hw_adc_channel_t channel, uint32_t sample_rate_hz);
esp_err_t hw_channel_set_buffer_size(hw_adc_channel_t channel, uint32_t buffer_size);

// TRIG 관리 함수
esp_err_t hw_trig_set_pin(gpio_num_t pin);
esp_err_t hw_trig_set_mode(hw_trig_mode_t mode);
esp_err_t hw_trig_set_edge(hw_trig_edge_t edge);
esp_err_t hw_trig_set_level(uint32_t level_mv);
esp_err_t hw_trig_set_pre_post_samples(uint32_t pre_samples, uint32_t post_samples);
esp_err_t hw_trig_enable(bool enable);

// ADC 모드 관리 함수
esp_err_t hw_adc_set_mode(hw_adc_mode_t mode);
esp_err_t hw_adc_start(void);
esp_err_t hw_adc_stop(void);

// 데이터 접근 함수 (32-bit 원본)
esp_err_t hw_get_channel_data_32bit(hw_adc_channel_t channel, uint32_t *raw_data, uint64_t *timestamps, uint32_t *data_count);
esp_err_t hw_get_latest_voltage(hw_adc_channel_t channel, uint32_t *voltage_mv);
esp_err_t hw_get_trig_event(hw_trig_event_t *trig_event);

// 16-bit 표시용 데이터 함수
esp_err_t hw_get_channel_data_16bit(hw_adc_channel_t channel, uint16_t *display_data, uint32_t *data_count);
esp_err_t hw_get_trigger_data_16bit(uint16_t *pre_data, uint16_t *post_data, uint64_t *pre_timestamps, uint64_t *post_timestamps);

// 통계 함수
esp_err_t hw_get_channel_statistics(hw_adc_channel_t channel, uint32_t *min_val, uint32_t *max_val, uint32_t *avg_val);

// 유틸리티 함수
uint32_t hw_raw_to_voltage(uint32_t raw_value, hw_attenuation_t attenuation);
uint32_t hw_voltage_to_raw(uint32_t voltage_mv, hw_attenuation_t attenuation);
const char* hw_get_channel_name(hw_adc_channel_t channel);
const char* hw_get_attenuation_name(hw_attenuation_t attenuation);

#ifdef __cplusplus
}
#endif

#endif // HARDWARE_MANAGER_H
