#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "adc_dma_continuous.h"

static const char *TAG = "ADC_DMA_TEST";

// ADC DMA 테스트 태스크
static void adc_dma_test_task(void *pvParameters)
{
    ESP_LOGI(TAG, "=== ADC DMA Continuous Mode Test Started ===");
    
    // ADC DMA Continuous Mode 초기화
    esp_err_t ret = adc_dma_continuous_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC DMA Continuous Mode: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
    
    // ADC DMA Continuous Mode 시작
    ret = adc_dma_continuous_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start ADC DMA Continuous Mode: %s", esp_err_to_name(ret));
        adc_dma_continuous_deinit();
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "ADC DMA Continuous Mode is running...");
    ESP_LOGI(TAG, "Sampling frequency: 10kHz");
    ESP_LOGI(TAG, "Buffer size: 1024 samples");
    ESP_LOGI(TAG, "Channels: ADC1_CH0 (GPIO36), ADC1_CH1 (GPIO37)");
    
    uint32_t test_count = 0;
    uint32_t channel_0_data[1024];
    uint32_t channel_1_data[1024];
    uint32_t data_count;
    
    while (1) {
        // ADC 데이터 가져오기
        ret = adc_dma_get_data(channel_0_data, channel_1_data, &data_count);
        if (ret == ESP_OK && data_count > 0) {
            test_count++;
            
            // 최신 전압값 가져오기
            uint32_t voltage_ch0_mv, voltage_ch1_mv;
            ret = adc_dma_get_latest_voltage(&voltage_ch0_mv, &voltage_ch1_mv);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Test #%lu - Latest: CH0=%lumV, CH1=%lumV", 
                        test_count, voltage_ch0_mv, voltage_ch1_mv);
            }
            
            // 통계 정보 가져오기
            uint32_t min_ch0, max_ch0, avg_ch0, min_ch1, max_ch1, avg_ch1;
            ret = adc_dma_get_statistics(&min_ch0, &max_ch0, &avg_ch0, &min_ch1, &max_ch1, &avg_ch1);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "CH0 Stats: Min=%lu, Max=%lu, Avg=%lu", min_ch0, max_ch0, avg_ch0);
                ESP_LOGI(TAG, "CH1 Stats: Min=%lu, Max=%lu, Avg=%lu", min_ch1, max_ch1, avg_ch1);
            }
            
            // 첫 번째 버퍼의 일부 데이터 출력 (디버깅용)
            if (test_count <= 3) {
                ESP_LOGI(TAG, "First 10 samples from buffer:");
                for (int i = 0; i < 10 && i < data_count; i++) {
                    ESP_LOGI(TAG, "Sample[%d]: CH0=%lu, CH1=%lu", 
                            i, channel_0_data[i], channel_1_data[i]);
                }
            }
            
            ESP_LOGI(TAG, "---");
        }
        
        // 5초마다 테스트
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        // 10번 테스트 후 종료 (선택사항)
        if (test_count >= 10) {
            ESP_LOGI(TAG, "Test completed after %lu iterations", test_count);
            break;
        }
    }
    
    // ADC DMA Continuous Mode 정지 및 정리
    adc_dma_continuous_stop();
    adc_dma_continuous_deinit();
    
    ESP_LOGI(TAG, "=== ADC DMA Continuous Mode Test Completed ===");
    vTaskDelete(NULL);
}

// ADC DMA 테스트 시작
esp_err_t start_adc_dma_test(void)
{
    ESP_LOGI(TAG, "Starting ADC DMA Continuous Mode test...");
    
    // 테스트 태스크 생성
    BaseType_t ret = xTaskCreate(adc_dma_test_task, "adc_dma_test", 8192, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ADC DMA test task");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

// 실시간 ADC 모니터링 태스크 (간단한 버전)
static void adc_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "=== ADC Real-time Monitor Started ===");
    
    // ADC DMA Continuous Mode 초기화
    esp_err_t ret = adc_dma_continuous_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC DMA Continuous Mode: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
    
    // ADC DMA Continuous Mode 시작
    ret = adc_dma_continuous_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start ADC DMA Continuous Mode: %s", esp_err_to_name(ret));
        adc_dma_continuous_deinit();
        vTaskDelete(NULL);
        return;
    }
    
    uint32_t monitor_count = 0;
    
    while (1) {
        // 최신 전압값만 가져오기 (빠른 모니터링)
        uint32_t voltage_ch0_mv, voltage_ch1_mv;
        ret = adc_dma_get_latest_voltage(&voltage_ch0_mv, &voltage_ch1_mv);
        if (ret == ESP_OK) {
            monitor_count++;
            printf("Monitor #%lu: CH0=%lumV, CH1=%lumV\n", 
                   monitor_count, voltage_ch0_mv, voltage_ch1_mv);
        }
        
        // 1초마다 업데이트
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// 실시간 ADC 모니터링 시작
esp_err_t start_adc_monitor(void)
{
    ESP_LOGI(TAG, "Starting ADC real-time monitor...");
    
    // 모니터링 태스크 생성
    BaseType_t ret = xTaskCreate(adc_monitor_task, "adc_monitor", 4096, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ADC monitor task");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}
