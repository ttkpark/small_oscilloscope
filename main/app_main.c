#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "OSCILLOSCOPE";

void app_main(void)
{
    ESP_LOGI(TAG, "Small Oscilloscope Starting...");
    
    // GPIO 초기화 (예시용)
    gpio_reset_pin(GPIO_NUM_2);
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    
    int led_state = 0;
    
    while (1) {
        ESP_LOGI(TAG, "Oscilloscope running... LED: %s", led_state ? "ON" : "OFF");
        
        // LED 토글 (ESP32 내장 LED)
        gpio_set_level(GPIO_NUM_2, led_state);
        led_state = !led_state;
        
        // 1초 대기
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
} 