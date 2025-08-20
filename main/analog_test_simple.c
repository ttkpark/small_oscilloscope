#include "analog_test_simple.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>
#include <inttypes.h>
#include <stdio.h>

static const char *TAG = "ANALOG_TEST_SIMPLE";

// 전역 변수
static channel_config_t g_channel_configs[ANALOG_CHANNELS];
static SemaphoreHandle_t g_config_mutex;

// I2C 주소 스캔 함수
esp_err_t i2c_scan_devices_simple(void) {
    ESP_LOGI(TAG, "=== I2C Address Scan (0x00-0x7F) ===");
    bool found_devices = false;
    uint8_t found_addresses[128];
    int found_count = 0;
    
    for (uint8_t addr = 0x00; addr <= 0x7F; addr++) {
        // I2C 주소는 7비트이므로 0x00-0x7F 범위
        uint8_t test_data = 0x00;
        esp_err_t scan_ret = i2c_master_write_to_device(I2C_NUM_0, addr, &test_data, 1, pdMS_TO_TICKS(100));
        
        if (scan_ret == ESP_OK) {
            ESP_LOGI(TAG, "I2C device found at address: 0x%02X", addr);
            found_devices = true;
            found_addresses[found_count++] = addr;
            
            // CH423의 예상 주소인지 확인
            if (addr == CH423_I2C_ADDR) {
                ESP_LOGI(TAG, "*** CH423 detected at expected address 0x%02X ***", addr);
            }
        }
    }
    
    // 스캔 결과 요약
    ESP_LOGI(TAG, "=== I2C Scan Summary ===");
    if (found_devices) {
        ESP_LOGI(TAG, "Found %d I2C device(s):", found_count);
        for (int i = 0; i < found_count; i++) {
            ESP_LOGI(TAG, "  - Address 0x%02X", found_addresses[i]);
        }
        
        // CH423 주소 확인
        bool ch423_found = false;
        for (int i = 0; i < found_count; i++) {
            if (found_addresses[i] == CH423_I2C_ADDR) {
                ch423_found = true;
                break;
            }
        }
        
        if (ch423_found) {
            ESP_LOGI(TAG, "✓ CH423 found at expected address 0x%02X", CH423_I2C_ADDR);
        } else {
            ESP_LOGW(TAG, "✗ CH423 not found at expected address 0x%02X", CH423_I2C_ADDR);
            ESP_LOGW(TAG, "  Check CH423 address configuration or hardware connection");
        }
    } else {
        ESP_LOGW(TAG, "No I2C devices found on the bus!");
        ESP_LOGW(TAG, "Please check:");
        ESP_LOGW(TAG, "1. I2C SDA/SCL connections (GPIO 21/22)");
        ESP_LOGW(TAG, "2. Power supply to CH423");
        ESP_LOGW(TAG, "3. Pull-up resistors on SDA/SCL lines");
        ESP_LOGW(TAG, "4. CH423 I2C address configuration");
        ESP_LOGW(TAG, "5. I2C bus speed (currently 100kHz)");
    }
    
    ESP_LOGI(TAG, "=== I2C Address Scan Complete ===");
    return found_devices ? ESP_OK : ESP_ERR_NOT_FOUND;
}

// CH423 테스트 함수 (간단 버전)
esp_err_t test_ch423_circuit_simple(uint8_t *read_data) {
    ESP_LOGI(TAG, "=== Testing CH423 Circuit (Simple) ===");
    
    if (!read_data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // I2C 초기화
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 22,  // I2C_SDA_PIN
        .scl_io_num = 21,  // I2C_SCL_PIN
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
        .clk_flags = 0,
    };
    
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
    
    /*// I2C 주소 스캔 실행
    ret = i2c_scan_devices_simple();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2C scan failed, but continuing with CH423 test");
    }*/
    
    // CH423 IO 출력 설정
    uint8_t cmd = CH423_CMD_IO_OUT;
    uint8_t data = 0x00;  // 모든 핀을 LOW로 설정
    ret = i2c_master_write_to_device(I2C_NUM_0, CH423_I2C_ADDR, &cmd, 1, pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CH423 IO output config failed: %s", esp_err_to_name(ret));
        i2c_driver_delete(I2C_NUM_0);
        return ret;
    }
    
    ret = i2c_master_write_to_device(I2C_NUM_0, CH423_I2C_ADDR, &data, 1, pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CH423 IO output data failed: %s", esp_err_to_name(ret));
        i2c_driver_delete(I2C_NUM_0);
        return ret;
    }
    
    // CH423에서 데이터 읽기
    cmd = CH423_CMD_IO_IN;
    ret = i2c_master_write_read_device(I2C_NUM_0, CH423_I2C_ADDR, &cmd, 1, read_data, 1, pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CH423 read failed: %s", esp_err_to_name(ret));
        i2c_driver_delete(I2C_NUM_0);
        return ret;
    }
    
    ESP_LOGI(TAG, "CH423 read data: 0x%02X", *read_data);
    
    /*
    // CH423 테스트 패턴 출력
    for (int i = 0; i < 8; i++) {
        data = (1 << i);
        ret = i2c_master_write_to_device(I2C_NUM_0, CH423_I2C_ADDR, &cmd, 1, pdMS_TO_TICKS(1000));
        if (ret == ESP_OK) {
            ret = i2c_master_write_to_device(I2C_NUM_0, CH423_I2C_ADDR, &data, 1, pdMS_TO_TICKS(1000));
        }
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "CH423 test pattern %d: 0x%02X", i, data);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }*/

    // 1차 1x, 2차 1x 출력
    // Q1|Q2|Q3|Q4|Q5|Q6|Q7|Q8|Q9|Q10|Q11|Q12
    //  1| 1| 1| 0| 0| 0| 0| 0| 0|  0|  0|  0
    // 0x41 0x00 
    // 0x03 0x00 
    data = 0x03;    
    //CH423 OC 출력 켜기
    ret = i2c_master_write_to_device(I2C_NUM_0, 0x22, &data, 1, pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CH423 OC output data failed: %s", esp_err_to_name(ret));
        i2c_driver_delete(I2C_NUM_0);
        return ret;
    }

    data = 0x00;   
    ret = i2c_master_write_to_device(I2C_NUM_0, 0x23, &data, 1, pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CH423 OC output data failed: %s", esp_err_to_name(ret));
        i2c_driver_delete(I2C_NUM_0);
        return ret;
    }

    do{
        data = 0xFF;    
        //CH423 OC 출력 켜기
        ret = i2c_master_write_to_device(I2C_NUM_0, 0x22, &data, 1, pdMS_TO_TICKS(1000));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "CH423 OC output data failed: %s", esp_err_to_name(ret));
            i2c_driver_delete(I2C_NUM_0);
            return ret;
        }
    
        ret = i2c_master_write_to_device(I2C_NUM_0, 0x23, &data, 1, pdMS_TO_TICKS(1000));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "CH423 OC output data failed: %s", esp_err_to_name(ret));
            i2c_driver_delete(I2C_NUM_0);
            return ret;
        }
        // CH423에서 데이터 읽기
        cmd = CH423_CMD_IO_IN;
        ret = i2c_master_write_read_device(I2C_NUM_0, CH423_I2C_ADDR, &cmd, 1, read_data, 1, pdMS_TO_TICKS(1000));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "CH423 read failed: %s", esp_err_to_name(ret));
            i2c_driver_delete(I2C_NUM_0);
            return ret;
        }
        ESP_LOGI(TAG, "CH423 read data: 0x%02X", *read_data);
        
    
        //vTaskDelay(pdMS_TO_TICKS(1000));

        data = 0x02;    
        //CH423 OC 출력 끄기
        ret = i2c_master_write_to_device(I2C_NUM_0, 0x22, &data, 1, pdMS_TO_TICKS(1000));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "CH423 OC output data failed: %s", esp_err_to_name(ret));
            i2c_driver_delete(I2C_NUM_0);
            return ret;
        }
        data = 0x00;    
    
        ret = i2c_master_write_to_device(I2C_NUM_0, 0x23, &data, 1, pdMS_TO_TICKS(1000));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "CH423 OC output data failed: %s", esp_err_to_name(ret));
            i2c_driver_delete(I2C_NUM_0);
            return ret;
        }
        // CH423에서 데이터 읽기
        cmd = CH423_CMD_IO_IN;
        ret = i2c_master_write_read_device(I2C_NUM_0, CH423_I2C_ADDR, &cmd, 1, read_data, 1, pdMS_TO_TICKS(1000));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "CH423 read failed: %s", esp_err_to_name(ret));
            i2c_driver_delete(I2C_NUM_0);
            return ret;
        }
        ESP_LOGI(TAG, "CH423 read data: 0x%02X", *read_data);
    


        //vTaskDelay(pdMS_TO_TICKS(1000));
        
    }while(0);
    
    i2c_driver_delete(I2C_NUM_0);
    return ESP_OK;
}

// 채널 설정 함수 (간단 버전)
esp_err_t set_channel_config_simple(uint8_t channel, const channel_config_t *config) {
    if (channel >= ANALOG_CHANNELS || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_config_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memcpy(&g_channel_configs[channel], config, sizeof(channel_config_t));
        xSemaphoreGive(g_config_mutex);
        
        ESP_LOGI(TAG, "Channel %d config: AC/DC=%d, Primary=%d, Secondary=%d, Voltage=%dmV, Enabled=%d",
                channel, config->ac_dc_mode, config->primary_atten, config->secondary_atten,
                config->output_voltage_mv, config->enabled);
        
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// 아날로그 출력 설정 함수 (간단 버전)
esp_err_t set_analog_output_simple(uint8_t channel, uint16_t voltage_mv) {
    if (channel >= ANALOG_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_config_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        g_channel_configs[channel].output_voltage_mv = voltage_mv;
        xSemaphoreGive(g_config_mutex);
        
        ESP_LOGI(TAG, "Channel %d output voltage set to %dmV", channel, voltage_mv);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// AC/DC 모드 설정 함수 (간단 버전)
esp_err_t set_ac_dc_mode_simple(uint8_t channel, ac_dc_mode_t mode) {
    if (channel >= ANALOG_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_config_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        g_channel_configs[channel].ac_dc_mode = mode;
        xSemaphoreGive(g_config_mutex);
        
        ESP_LOGI(TAG, "Channel %d AC/DC mode set to %s", channel, mode == MODE_AC ? "AC" : "DC");
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// 감쇠율 설정 함수 (간단 버전)
esp_err_t set_attenuation_simple(uint8_t channel, attenuation_t primary, attenuation_t secondary) {
    if (channel >= ANALOG_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_config_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        g_channel_configs[channel].primary_atten = primary;
        g_channel_configs[channel].secondary_atten = secondary;
        xSemaphoreGive(g_config_mutex);
        
        ESP_LOGI(TAG, "Channel %d attenuation set to Primary=%d, Secondary=%d", channel, primary, secondary);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// 시리얼 출력 태스크 (간단 버전)
static void serial_output_task_simple(void *pvParameters) {
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    
    esp_err_t ret = adc_oneshot_new_unit(&init_cfg, &adc1_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC unit init failed in serial task");
        vTaskDelete(NULL);
        return;
    }
    
    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_0, &chan_cfg);
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_1, &chan_cfg);
    
    uint32_t adc_readings[ANALOG_CHANNELS];
    channel_config_t current_configs[ANALOG_CHANNELS];
    
    while (1) {
        // ADC 읽기
        for (int ch = 0; ch < ANALOG_CHANNELS; ch++) {
            int raw_value;
            adc_oneshot_read(adc1_handle, (ch == 0) ? ADC_CHANNEL_0 : ADC_CHANNEL_1, &raw_value);
            adc_readings[ch] = (raw_value * 3300) / 4095;  // mV로 변환
        }
        
        // 현재 설정 가져오기
        if (xSemaphoreTake(g_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            memcpy(current_configs, g_channel_configs, sizeof(g_channel_configs));
            xSemaphoreGive(g_config_mutex);
        }
        
        // 시리얼 출력
        printf("ADC: CH0=%lumV, CH1=%lumV | ", adc_readings[0], adc_readings[1]);
        for (int ch = 0; ch < ANALOG_CHANNELS; ch++) {
            printf("CH%d: %s, %dX/%dX, %dmV | ", 
                   ch, 
                   current_configs[ch].ac_dc_mode == MODE_AC ? "AC" : "DC",
                   current_configs[ch].primary_atten == ATTEN_1X ? 1 : 
                   current_configs[ch].primary_atten == ATTEN_10X ? 10 :
                   current_configs[ch].primary_atten == ATTEN_100X ? 100 : 1000,
                   current_configs[ch].secondary_atten == ATTEN_1X ? 1 : 
                   current_configs[ch].secondary_atten == ATTEN_10X ? 10 :
                   current_configs[ch].secondary_atten == ATTEN_100X ? 100 : 1000,
                   current_configs[ch].output_voltage_mv);
        }
        printf("\n");
        
        vTaskDelay(pdMS_TO_TICKS(100));  // 10Hz 업데이트
    }
}

// 시리얼 출력 태스크 시작 (간단 버전)
esp_err_t start_serial_output_task_simple(void) {
    xTaskCreate(serial_output_task_simple, "serial_output_simple", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Serial output task started (simple version)");
    return ESP_OK;
}

// 아날로그 테스트 초기화 (간단 버전)
esp_err_t analog_test_simple_init(void) {
    ESP_LOGI(TAG, "Initializing analog test system (simple version)");
    
    // 뮤텍스 생성
    g_config_mutex = xSemaphoreCreateMutex();
    if (!g_config_mutex) {
        ESP_LOGE(TAG, "Failed to create config mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // 채널 설정 초기화
    for (int i = 0; i < ANALOG_CHANNELS; i++) {
        g_channel_configs[i].ac_dc_mode = MODE_DC;
        g_channel_configs[i].primary_atten = ATTEN_1X;
        g_channel_configs[i].secondary_atten = ATTEN_1X;
        g_channel_configs[i].output_voltage_mv = 1000;
        g_channel_configs[i].enabled = true;
    }
    
    ESP_LOGI(TAG, "Analog test system initialized (simple version)");
    return ESP_OK;
}

// 종합 테스트 실행 (간단 버전)
esp_err_t run_analog_comprehensive_test_simple(analog_test_results_t *results) {
    if (!results) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "=== Running Analog Comprehensive Test (Simple) ===");
    
    // 결과 초기화
    memset(results, 0, sizeof(analog_test_results_t));
    
    // CH423 테스트
    results->ch423_test_passed = (test_ch423_circuit_simple(&results->ch423_read_data) == ESP_OK);
    
    // ADC 읽기 테스트
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    
    esp_err_t ret = adc_oneshot_new_unit(&init_cfg, &adc1_handle);
    if (ret == ESP_OK) {
        adc_oneshot_chan_cfg_t chan_cfg = {
            .bitwidth = ADC_BITWIDTH_DEFAULT,
            .atten = ADC_ATTEN_DB_12,
        };
        adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_0, &chan_cfg);
        adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_1, &chan_cfg);
        
        for (int ch = 0; ch < ANALOG_CHANNELS; ch++) {
            int raw_value;
            adc_oneshot_read(adc1_handle, (ch == 0) ? ADC_CHANNEL_0 : ADC_CHANNEL_1, &raw_value);
            results->adc_readings[ch] = (raw_value * 3300) / 4095;
        }
        
        adc_oneshot_del_unit(adc1_handle);
        results->analog_output_test_passed = true;
    }
    
    // 설정 복사
    memcpy(results->channel_configs, g_channel_configs, sizeof(g_channel_configs));
    
    ESP_LOGI(TAG, "Analog comprehensive test completed (simple version)");
    return ESP_OK;
}

// 테스트 결과 출력 (간단 버전)
void print_analog_test_results_simple(const analog_test_results_t *results) {
    if (!results) {
        ESP_LOGW(TAG, "No test results to print");
        return;
    }
    
    ESP_LOGI(TAG, "=== Analog Test Results Summary ===");
    ESP_LOGI(TAG, "CH423 Test: %s", results->ch423_test_passed ? "PASSED" : "FAILED");
    ESP_LOGI(TAG, "Analog Output Test: %s", results->analog_output_test_passed ? "PASSED" : "FAILED");
    ESP_LOGI(TAG, "Serial Output Test: %s", results->serial_output_test_passed ? "PASSED" : "FAILED");
    ESP_LOGI(TAG, "CH423 Read Data: 0x%02X", results->ch423_read_data);
    
    for (int i = 0; i < ANALOG_CHANNELS; i++) {
        ESP_LOGI(TAG, "Channel %d Config: AC/DC=%s, Primary=%dX, Secondary=%dX, Voltage=%dmV, Enabled=%s",
                 i,
                 results->channel_configs[i].ac_dc_mode == MODE_DC ? "DC" : "AC",
                 results->channel_configs[i].primary_atten == ATTEN_1X ? 1 : 
                 results->channel_configs[i].primary_atten == ATTEN_10X ? 10 : 
                 results->channel_configs[i].primary_atten == ATTEN_100X ? 100 : 1000,
                 results->channel_configs[i].secondary_atten == ATTEN_1X ? 1 : 
                 results->channel_configs[i].secondary_atten == ATTEN_10X ? 10 : 
                 results->channel_configs[i].secondary_atten == ATTEN_100X ? 100 : 1000,
                 results->channel_configs[i].output_voltage_mv,
                 results->channel_configs[i].enabled ? "Yes" : "No");
    }
    ESP_LOGI(TAG, "=== End Test Results ===");
}

// LED 제어 함수들
esp_err_t set_led_state_simple(uint8_t led_id, bool state) {
    if (led_id > 3) {
        ESP_LOGE(TAG, "Invalid LED ID: %d", led_id);
        return ESP_ERR_INVALID_ARG;
    }
    
    // CH423 OC 출력 제어
    // LED0: OC12, LED1: OC13, LEDRE0: OC14, LEDRE1: OC15
    uint8_t oc_addr = 0x22;  // OC0-OC7
    uint8_t oc_data = 0x00;
    
    if (led_id == 0) {
        // LED0 (OC12) - 0x23의 bit 4
        oc_addr = 0x23;
        oc_data = state ? 0x10 : 0x00;
    } else if (led_id == 1) {
        // LED1 (OC13) - 0x23의 bit 5
        oc_addr = 0x23;
        oc_data = state ? 0x20 : 0x00;
    } else if (led_id == 2) {
        // LEDRE0 (OC14) - 0x23의 bit 6
        oc_addr = 0x23;
        oc_data = state ? 0x40 : 0x00;
    } else if (led_id == 3) {
        // LEDRE1 (OC15) - 0x23의 bit 7
        oc_addr = 0x23;
        oc_data = state ? 0x80 : 0x00;
    }
    
    esp_err_t ret = i2c_master_write_to_device(I2C_NUM_0, oc_addr, &oc_data, 1, pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED %d set failed: %s", led_id, esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "LED %d set to %s", led_id, state ? "ON" : "OFF");
    return ESP_OK;
}

esp_err_t set_backlight_state_simple(bool state) {
    // BACKLIGHT는 CH423s-OC1에 연결됨
    uint8_t oc_addr = 0x22;  // OC0-OC7
    uint8_t oc_data = state ? 0x02 : 0x00;  // OC1 (bit 1)
    
    esp_err_t ret = i2c_master_write_to_device(I2C_NUM_0, oc_addr, &oc_data, 1, pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Backlight set failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Backlight set to %s", state ? "ON" : "OFF");
    return ESP_OK;
}

esp_err_t blink_backlight_simple(uint8_t count, uint32_t on_time_ms, uint32_t off_time_ms) {
    ESP_LOGI(TAG, "Starting backlight blink: %d times, ON=%lu ms, OFF=%lu ms", count, on_time_ms, off_time_ms);
    
    for (uint8_t i = 0; i < count; i++) {
        // 켜기
        esp_err_t ret = set_backlight_state_simple(true);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Backlight ON failed: %s", esp_err_to_name(ret));
            return ret;
        }
        vTaskDelay(pdMS_TO_TICKS(on_time_ms));
        
        // 끄기
        ret = set_backlight_state_simple(false);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Backlight OFF failed: %s", esp_err_to_name(ret));
            return ret;
        }
        vTaskDelay(pdMS_TO_TICKS(off_time_ms));
    }
    
    ESP_LOGI(TAG, "Backlight blink completed");
    return ESP_OK;
} 