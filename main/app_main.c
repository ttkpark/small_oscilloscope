#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_adc_cal.h"
#include "esp_timer.h"
#include "oscilloscope_test.h"
#include "ft800.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <inttypes.h>

static const char *TAG = "LCD_TEST";

// FT800 핸들
static ft800_handle_t lcd;

void app_main(void) {
    ESP_LOGI(TAG, "LCD Test Starting...");
    
    // FT800 LCD 초기화 (MOSI 핀을 19로 변경)
    esp_err_t ret = ft800_init(&lcd, SPI2_HOST, 23, 18, 5);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FT800 init failed: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "FT800 LCD initialized successfully");
    

    ESP_LOGI(TAG, "%02x %02x %02x %02x", ft800_read8(&lcd, 0x0C0000), ft800_read8(&lcd, 0x0C0001), ft800_read8(&lcd, 0x0C0002), ft800_read8(&lcd, 0x0C0003));
    // 칩 ID 재확인
    ret = ft800_check_id(&lcd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FT800 chip ID verification failed");
        return;
    }
    
    // FT800 백라이트 켜기
    ft800_backlight_on(&lcd);
    ESP_LOGI(TAG, "FT800 Backlight turned ON");
    
    // LCD 화면 켜기
    ft800_cmd_start(&lcd);
    ft800_cmd_clear(&lcd, 1, 1, 1);  // 흰색 배경
    ft800_cmd_text(&lcd, 240, 136, 31, 0, "Small Oscilloscope");  // 중앙에 텍스트
    ft800_cmd_text(&lcd, 240, 160, 26, 0, "LCD Test");  // 중앙에 텍스트
    ft800_cmd_text(&lcd, 240, 184, 26, 0, "Success!");  // 중앙에 텍스트
    ft800_cmd_swap(&lcd);
    
    ESP_LOGI(TAG, "LCD display content set");
    
    // 백라이트 밝기 조절 테스트
    uint8_t brightness = 128;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        
        // 백라이트 밝기 조절 (128 -> 64 -> 32 -> 0 -> 128)
        brightness = (brightness == 0) ? 128 : (brightness / 2);
        ft800_backlight(&lcd, brightness);
        ESP_LOGI(TAG, "FT800 Backlight brightness: %d/128", brightness);
    }
} 