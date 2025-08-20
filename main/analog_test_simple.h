#ifndef ANALOG_TEST_SIMPLE_H
#define ANALOG_TEST_SIMPLE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// CH423 관련 정의
#define CH423_I2C_ADDR          0x20//0x40
#define CH423_CMD_IO_OUT        0x48
#define CH423_CMD_IO_IN         0x49
#define CH423_CMD_IO_DIR        0x4A

// LED 정의 (CH423s-OC12~OC15)
#define LED0_BIT                (1 << 12)  // OC12 - GP LED (Red)
#define LED1_BIT                (1 << 13)  // OC13 - GP LED (Green)
#define LEDRE0_BIT              (1 << 14)  // OC14 - Rotary Encoder 0 LED (Green)
#define LEDRE1_BIT              (1 << 15)  // OC15 - Rotary Encoder 1 LED (Green)

// 아날로그 출력 채널 정의
#define ANALOG_CHANNELS         2
#define CHANNEL_0               0
#define CHANNEL_1               1

// AC/DC 모드 정의
typedef enum {
    MODE_DC = 0,
    MODE_AC = 1
} ac_dc_mode_t;

// 감쇠율 정의
typedef enum {
    ATTEN_1X = 0,    // 1배 (감쇠 없음)
    ATTEN_10X = 1,   // 10배 감쇠
    ATTEN_100X = 2,  // 100배 감쇠
    ATTEN_1000X = 3  // 1000배 감쇠
} attenuation_t;

// 채널 설정 구조체
typedef struct {
    ac_dc_mode_t ac_dc_mode;
    attenuation_t primary_atten;
    attenuation_t secondary_atten;
    uint16_t output_voltage_mv;  // 출력 전압 (mV)
    bool enabled;
} channel_config_t;

// 아날로그 테스트 결과 구조체
typedef struct {
    bool ch423_test_passed;
    bool analog_output_test_passed;
    bool serial_output_test_passed;
    uint8_t ch423_read_data;
    uint32_t adc_readings[ANALOG_CHANNELS];
    channel_config_t channel_configs[ANALOG_CHANNELS];
} analog_test_results_t;

// 함수 선언
esp_err_t analog_test_simple_init(void);
esp_err_t i2c_scan_devices_simple(void);
esp_err_t test_ch423_circuit_simple(uint8_t *read_data);
esp_err_t set_channel_config_simple(uint8_t channel, const channel_config_t *config);
esp_err_t set_analog_output_simple(uint8_t channel, uint16_t voltage_mv);
esp_err_t set_ac_dc_mode_simple(uint8_t channel, ac_dc_mode_t mode);
esp_err_t set_attenuation_simple(uint8_t channel, attenuation_t primary, attenuation_t secondary);
esp_err_t start_serial_output_task_simple(void);
esp_err_t run_analog_comprehensive_test_simple(analog_test_results_t *results);
void print_analog_test_results_simple(const analog_test_results_t *results);

// LED 제어 함수들
esp_err_t set_led_state_simple(uint8_t led_id, bool state);
esp_err_t set_backlight_state_simple(bool state);
esp_err_t blink_backlight_simple(uint8_t count, uint32_t on_time_ms, uint32_t off_time_ms);

#endif // ANALOG_TEST_SIMPLE_H 