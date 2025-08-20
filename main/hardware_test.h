#ifndef HARDWARE_TEST_H
#define HARDWARE_TEST_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// 테스트 결과 구조체
typedef struct {
    bool ch423_ok;
    bool lcd_ok;
    bool buttons_ok;
    bool leds_ok;
    bool rotary_encoders_ok;
    bool adc_ok;
    bool dac_ok;
    bool trigger_ok;
    bool battery_ok;
    bool relays_ok;
} hardware_test_results_t;

// 종합 하드웨어 테스트 실행
esp_err_t run_hardware_test(hardware_test_results_t *results);

// 테스트 결과 출력
void print_hardware_test_results(const hardware_test_results_t *results);

esp_err_t ch423_set_output(uint8_t pin, bool state);
esp_err_t ch423_read_input(uint8_t *data);

// ADC 함수들 (기존 호환성 유지)
uint16_t get_adc_latest_value(void);
uint16_t* get_adc_buffer(void);
int get_adc_buffer_index(void);
uint16_t get_adc_latest_value1(void);
uint16_t get_adc_latest_value2(void);

// 새로운 ADC DMA 전용 함수들
uint32_t* get_adc_dma_buffer_ch0(void);
uint32_t* get_adc_dma_buffer_ch1(void);
bool is_adc_dma_enabled(void);
esp_err_t get_adc_statistics(uint32_t *min_ch0, uint32_t *max_ch0, uint32_t *avg_ch0,
                            uint32_t *min_ch1, uint32_t *max_ch1, uint32_t *avg_ch1);

#endif // HARDWARE_TEST_H
