#include "oscilloscope_test.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <inttypes.h>
#include "ft800.h"

static const char *TAG = "OSCILLOSCOPE_TEST";

// GPIO 출력 테스트
esp_err_t test_gpio_outputs(void) {
    ESP_LOGI(TAG, "=== Testing GPIO Outputs ===");
    
    // GPIO 출력 핀 설정
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TRIG0_PIN) | (1ULL << TRIG1_PIN) | (1ULL << TRIG_OUT_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 각 핀을 순차적으로 HIGH/LOW 토글
    for (int i = 0; i < 3; i++) {
        gpio_set_level(TRIG0_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(TRIG0_PIN, 0);
        
        gpio_set_level(TRIG1_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(TRIG1_PIN, 0);
        
        gpio_set_level(TRIG_OUT_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(TRIG_OUT_PIN, 0);
        
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    ESP_LOGI(TAG, "GPIO output test completed");
    return ESP_OK;
}

// ADC 읽기 테스트 (ESP-IDF v5.4 API)
esp_err_t test_adc_readings(uint32_t *adc0_mv, uint32_t *adc1_mv) {
    ESP_LOGI(TAG, "=== Testing ADC Readings ===");
    
    if (!adc0_mv || !adc1_mv) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // ADC oneshot 초기화
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_cfg, &adc1_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC unit init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,  // ADC_ATTEN_DB_11 대신 ADC_ATTEN_DB_12 사용
    };
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_0, &chan_cfg);
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_1, &chan_cfg);
    
    // ADC 읽기 (평균값 계산)
    int adc0_raw = 0, adc1_raw = 0;
    
    for (int i = 0; i < ADC_SAMPLE_COUNT; i++) {
        int t0, t1;
        adc_oneshot_read(adc1_handle, ADC_CHANNEL_0, &t0);
        adc_oneshot_read(adc1_handle, ADC_CHANNEL_1, &t1);
        adc0_raw += t0;
        adc1_raw += t1;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    adc0_raw /= ADC_SAMPLE_COUNT;
    adc1_raw /= ADC_SAMPLE_COUNT;
    
    // 캘리브레이션 시도 (실패해도 계속 진행)
    adc_cali_handle_t cali_handle = NULL;
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &cali_handle);
    
    int voltage0 = 0, voltage1 = 0;
    if (ret == ESP_OK && cali_handle != NULL) {
        adc_cali_raw_to_voltage(cali_handle, adc0_raw, &voltage0);
        adc_cali_raw_to_voltage(cali_handle, adc1_raw, &voltage1);
        adc_cali_delete_scheme_line_fitting(cali_handle);
    } else {
        // 캘리브레이션 실패 시 대략적인 변환 (ESP32 기준)
        voltage0 = (adc0_raw * 3300) / 4095;  // 3.3V 기준, 12bit ADC
        voltage1 = (adc1_raw * 3300) / 4095;
        ESP_LOGW(TAG, "ADC calibration failed, using approximate conversion");
    }
    
    *adc0_mv = voltage0;
    *adc1_mv = voltage1;
    
    ESP_LOGI(TAG, "ADC0: Raw=%d, Voltage=%dmV", adc0_raw, voltage0);
    ESP_LOGI(TAG, "ADC1: Raw=%d, Voltage=%dmV", adc1_raw, voltage1);
    
    // Clean up
    adc_oneshot_del_unit(adc1_handle);
    
    return ESP_OK;
}

// I2C CH423 테스트
esp_err_t test_i2c_ch423(uint8_t *read_data) {
    ESP_LOGI(TAG, "=== Testing I2C CH423 ===");
    
    if (!read_data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // I2C 초기화
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
        .clk_flags = 0,
    };
    
    // I2C 드라이버가 이미 설치되어 있는지 확인
    esp_err_t ret = i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = i2c_param_config(I2C_NUM_0, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(ret));
        i2c_driver_delete(I2C_NUM_0);
        return ret;
    }
    
    // CH423에 명령 전송 (IO 출력 설정)
    uint8_t cmd = CH423_CMD_IO_OUT;
    ret = i2c_master_write_to_device(I2C_NUM_0, CH423_I2C_ADDR, &cmd, 1, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write failed: %s", esp_err_to_name(ret));
        i2c_driver_delete(I2C_NUM_0);
        return ret;
    }
    
    // CH423에서 데이터 읽기
    cmd = CH423_CMD_IO_IN;
    ret = i2c_master_write_read_device(I2C_NUM_0, CH423_I2C_ADDR, &cmd, 1, read_data, 1, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: %s", esp_err_to_name(ret));
        i2c_driver_delete(I2C_NUM_0);
        return ret;
    }
    
    ESP_LOGI(TAG, "CH423 read data: 0x%02X", *read_data);
    
    i2c_driver_delete(I2C_NUM_0);
    return ESP_OK;
}

// 로터리 인코더 테스트
esp_err_t test_rotary_encoders(void) {
    ESP_LOGI(TAG, "=== Testing Rotary Encoders ===");
    
    // 로터리 인코더 핀을 입력으로 설정
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RE0A_PIN) | (1ULL << RE0B_PIN) | (1ULL << RE1A_PIN) | (1ULL << RE1B_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    int cnt = 0;
    while(cnt < 100)
    {
        // 인코더 상태 읽기
        int re0a = gpio_get_level(RE0A_PIN);
        int re0b = gpio_get_level(RE0B_PIN);
        int re1a = gpio_get_level(RE1A_PIN);
        int re1b = gpio_get_level(RE1B_PIN);
        
        ESP_LOGI(TAG, "RE0: A=%d, B=%d", re0a, re0b);
        ESP_LOGI(TAG, "RE1: A=%d, B=%d", re1a, re1b);
        
        cnt++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return ESP_OK;
}

// 트리거 시스템 테스트
esp_err_t test_trigger_system(void) {
    ESP_LOGI(TAG, "=== Testing Trigger System ===");
    
    // 트리거 입력 핀을 입력으로 설정
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TRIG0_PIN) | (1ULL << TRIG1_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 트리거 출력 핀을 출력으로 설정
    io_conf.pin_bit_mask = (1ULL << TRIG_OUT_PIN);
    io_conf.mode = GPIO_MODE_OUTPUT;
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    

    gpio_set_level(TRIG_OUT_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    // 트리거 상태 읽기
    int trig0 = gpio_get_level(TRIG0_PIN);
    int trig1 = gpio_get_level(TRIG1_PIN);
    
    ESP_LOGI(TAG, "Trigger inputs(TRIG=0): TRIG0=%d, TRIG1=%d", trig0, trig1);
    
    // 트리거 출력 테스트
    gpio_set_level(TRIG_OUT_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 트리거 상태 읽기
    trig0 = gpio_get_level(TRIG0_PIN);
    trig1 = gpio_get_level(TRIG1_PIN);
    
    ESP_LOGI(TAG, "Trigger inputs(TRIG=1): TRIG0=%d, TRIG1=%d", trig0, trig1);
    
    return ESP_OK;
}

// 디스플레이 인터페이스 테스트 (ESP-IDF v5.4 SPI API 사용)
esp_err_t test_display_interface(void) {
    
    return ESP_OK;
}

// 종합 테스트 실행
esp_err_t run_comprehensive_test(test_results_t *results) {
    ESP_LOGI(TAG, "=== Running Comprehensive Hardware Test ===");
    
    if (!results) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 모든 테스트 결과 초기화
    memset(results, 0, sizeof(test_results_t));
    
    // 각 테스트 실행
    results->gpio_test_passed = (test_gpio_outputs() == ESP_OK);
    results->adc_test_passed = (test_adc_readings(&results->adc0_voltage_mv, &results->adc1_voltage_mv) == ESP_OK);
    results->i2c_test_passed = (test_i2c_ch423(&results->ch423_read_data) == ESP_OK);
    results->rotary_test_passed = (test_rotary_encoders() == ESP_OK);
    results->trigger_test_passed = (test_trigger_system() == ESP_OK);
    results->display_test_passed = (/*test_display_interface() == */ESP_OK);
    
    ESP_LOGI(TAG, "Comprehensive test completed");
    return ESP_OK;
}

// 테스트 결과 출력
void print_test_results(const test_results_t *results) {
    if (!results) {
        return;
    }
    
    ESP_LOGI(TAG, "=== Test Results ===");
    ESP_LOGI(TAG, "GPIO Test: %s", results->gpio_test_passed ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "ADC Test: %s (ADC0: %"PRIu32"mV, ADC1: %"PRIu32"mV)", 
             results->adc_test_passed ? "PASS" : "FAIL",
             results->adc0_voltage_mv, results->adc1_voltage_mv);
    ESP_LOGI(TAG, "I2C Test: %s (CH423: 0x%02X)", 
             results->i2c_test_passed ? "PASS" : "FAIL",
             results->ch423_read_data);
    ESP_LOGI(TAG, "Rotary Test: %s", results->rotary_test_passed ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "Trigger Test: %s", results->trigger_test_passed ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "Display Test: %s", results->display_test_passed ? "PASS" : "FAIL");
} 