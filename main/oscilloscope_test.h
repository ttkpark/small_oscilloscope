#ifndef OSCILLOSCOPE_TEST_H
#define OSCILLOSCOPE_TEST_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Pin definitions
#define I2C_SCL_PIN         22
#define I2C_SDA_PIN         21
#define SPI_SS_PIN          5
#define SPI_MOSI_PIN        23
#define SPI_MISO_PIN        -1
#define SPI_SCLK_PIN        18
#define TRIG0_PIN           17
#define TRIG1_PIN           16
#define RE1A_PIN            14
#define RE1B_PIN            27
#define RE0A_PIN            13
#define RE0B_PIN            12
#define TRIG_OUT_PIN        25
#define ADC0_PIN            ADC1_CHANNEL_0  // GPIO36
#define ADC1_PIN            ADC1_CHANNEL_1  // GPIO37

// CH423 I2C address and commands
#define CH423_I2C_ADDR      0x40
#define CH423_CMD_IO_OUT    0x48
#define CH423_CMD_IO_IN     0x49
#define CH423_CMD_IO_DIR    0x4A

// Test configuration
#define ADC_SAMPLE_COUNT    10
#define I2C_TIMEOUT_MS      1000
#define SPI_CLOCK_HZ        1000000

// Test results structure
typedef struct {
    bool gpio_test_passed;
    bool adc_test_passed;
    bool i2c_test_passed;
    bool rotary_test_passed;
    bool trigger_test_passed;
    bool display_test_passed;
    uint32_t adc0_voltage_mv;
    uint32_t adc1_voltage_mv;
    uint8_t ch423_read_data;
} test_results_t;

// Function declarations
esp_err_t test_gpio_outputs(void);
esp_err_t test_adc_readings(uint32_t *adc0_mv, uint32_t *adc1_mv);
esp_err_t test_i2c_ch423(uint8_t *read_data);
esp_err_t test_rotary_encoders(void);
esp_err_t test_trigger_system(void);
esp_err_t test_display_interface(void);
esp_err_t run_comprehensive_test(test_results_t *results);
void print_test_results(const test_results_t *results);

#endif // OSCILLOSCOPE_TEST_H 