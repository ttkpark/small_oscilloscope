#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "oscilloscope_test.h"
#include "ft800.h"
#include "analog_test_simple.h"
#include "hardware_test.h"
#include "interactive_test.h"
#include "adc_dma_test.h"
//#include "esp_adc/adc_oneshot.h"
//#include "esp_adc/adc_cali.h"
//#include "esp_adc/adc_cali_scheme.h"
#include <inttypes.h>

static const char *TAG = "SMALL_OSCILLOSCOPE";

// FT800 핸들
static ft800_handle_t lcd;

// FT800 인터럽트 핸들러
static void IRAM_ATTR ft800_isr_handler(void* arg) {
    // 인터럽트 처리 로직
    // 여기서는 간단히 플래그만 설정
    (void)arg;  // 사용하지 않는 변수 경고 제거
}

// FT800 화면 테스트 태스크
static void ft800_test_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting FT800 display test task");
    

    // FT800 초기화
    if (!initFT800()) {
        ESP_LOGI(TAG, "FT800 test passed");
    } else {
        ESP_LOGE(TAG, "FT800 test failed");
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    spi_speedup();
    
	clrscr();
    

	lcd_start_screen(0);
	cmd(DISPLAY());
	cmd(CMD_SWAP);	
    
    ESP_LOGI(TAG, "FT800 display test completed successfully");
    
    // 메인 루프 - 주기적으로 색상 변경
    int test_phase = 0;
    while (1) {
		static uint32_t frames = 0;
        uint32_t frames_old = frames;
        frames = HOST_MEM_RD32(REG_FRAMES);
        if(frames_old != frames)
        {
            lcd_start_screen(frames);
            //ESP_LOGI(TAG, "FRAMES: %ld", frames);

            char str[100];
            sprintf(str, "FRAMES: %ld", frames);
            cmd(COLOR_RGB(0xDE,0xDE,0xDE));
            cmd_text(10,230, 27,0, str);
            
            memset(str, 0, sizeof(str));
            sprintf(str, "GPIO12 = %d", gpio_get_level(12));
            cmd(COLOR_RGB(0xDE,0xDE,0xDE));
            cmd_text(470,230, 26,OPT_RIGHTX, str);

            cmd(DISPLAY());
            cmd(CMD_SWAP);	

            if((frames%60) == 10){
                //ESP_LOGI(TAG, "FRAMES: %ld", frames);
                HOST_MEM_WR8(REG_VOL_SOUND, 0xFF);      	
                switch((frames%180)/60){
                    case 0:
                        HOST_MEM_WR16(REG_SOUND, 0x50);      	//C8 MIDI xylophone
                        break;
                    case 1:
                        HOST_MEM_WR16(REG_SOUND, 0x51);      	//C8 MIDI xylophone
                        break;
                    case 2:
                        HOST_MEM_WR16(REG_SOUND, 0x56);      	//C8 MIDI xylophone
                        break;
                }
                HOST_MEM_WR8(REG_PLAY, 1); 
            }
            
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// 아날로그 테스트 태스크 (간단 버전)
static void analog_test_task_simple(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP_LOGI(TAG, "Starting analog test task (simple version)");
    
    // 아날로그 테스트 시스템 초기화
    esp_err_t ret = analog_test_simple_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Analog test init failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
    
    // 시리얼 출력 태스크 시작
    ret = start_serial_output_task_simple();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Serial output task start failed: %s", esp_err_to_name(ret));
    }
    /*
    while(1)
    {
        
        // CH423 테스트 실행
        uint8_t ch423_data;
        ret = test_ch423_circuit_simple(&ch423_data);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "CH423 test passed, data: 0x%02X", ch423_data);
        } else {
            ESP_LOGE(TAG, "CH423 test failed: %s", esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    */
    // CH423 테스트 실행
    uint8_t ch423_data;
    ret = test_ch423_circuit_simple(&ch423_data);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "CH423 test passed, data: 0x%02X", ch423_data);
    } else {
        ESP_LOGE(TAG, "CH423 test failed: %s", esp_err_to_name(ret));
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 채널 설정 예제
    channel_config_t config;
    
    // 채널 0 설정: DC 모드, 1X/10X 감쇠, 1500mV 출력
    config.ac_dc_mode = MODE_DC;
    config.primary_atten = ATTEN_1X;
    config.secondary_atten = ATTEN_10X;
    config.output_voltage_mv = 1500;
    config.enabled = true;
    set_channel_config_simple(0, &config);
    
    // 채널 1 설정: AC 모드, 10X/100X 감쇠, 2000mV 출력
    config.ac_dc_mode = MODE_AC;
    config.primary_atten = ATTEN_10X;
    config.secondary_atten = ATTEN_100X;
    config.output_voltage_mv = 2000;
    config.enabled = true;
    set_channel_config_simple(1, &config);
    
    // 주기적으로 종합 테스트 실행
    /*while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));  // 10초마다
        
        ret = run_analog_comprehensive_test_simple(&test_results);
        if (ret == ESP_OK) {
            print_analog_test_results_simple(&test_results);
        }
    }*/
    
    // BACKLIGHT 깜빡임 테스트
    ESP_LOGI(TAG, "Starting BACKLIGHT blink test...");
    ret = blink_backlight_simple(5, 1000, 500);  // 5번 깜빡임, ON=1초, OFF=0.5초
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BACKLIGHT blink test failed: %s", esp_err_to_name(ret));
    }
    
    // LED 테스트
    ESP_LOGI(TAG, "Starting LED test...");
    for (int i = 0; i < 4; i++) {
        ret = set_led_state_simple(i, true);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "LED %d turned ON", i);
            vTaskDelay(pdMS_TO_TICKS(500));
            ret = set_led_state_simple(i, false);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "LED %d turned OFF", i);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// 하드웨어 테스트 태스크
static void hardware_test_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting complete hardware test...");
    
    // 하드웨어 테스트 실행
    hardware_test_results_t test_results;
    esp_err_t ret = run_hardware_test(&test_results);
    
    if (ret == ESP_OK) {
        print_hardware_test_results(&test_results);
        ESP_LOGI(TAG, "Hardware test completed successfully");
    } else {
        ESP_LOGE(TAG, "Hardware test failed: %s", esp_err_to_name(ret));
    }
    // 상호작용 테스트 시작
    start_interactive_test();
    
    // 테스트 완료 후 태스크 종료
    vTaskDelete(NULL);
}

void app_main(void) {
    ESP_LOGI(TAG, "Small Oscilloscope Starting (Simple Version)...");
    
    // 아날로그 테스트 태스크 시작 (간단 버전)
    //xTaskCreate(analog_test_task_simple, "analog_test_simple", 8192, NULL, 5, NULL);
    // GPIO 출력 핀 설정

    /*gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << 12),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
        return ;
    }

    // FT800 화면 테스트 태스크 시작
    xTaskCreate(ft800_test_task, "ft800_test_task", 8192, NULL, 5, NULL);
    */

    /*// 기존 테스트도 실행 (선택사항)
    ESP_LOGI(TAG, "Running original oscilloscope tests...");
    test_results_t original_results;
    ret = run_comprehensive_test(&original_results);
    if (ret == ESP_OK) {
        print_test_results(&original_results);
    }*/
    
    // 하드웨어 테스트 태스크 시작
    xTaskCreate(hardware_test_task, "hardware_test_task", 8192, NULL, 5, NULL);
    
    // ADC DMA Continuous Mode 테스트 시작 (선택사항)
    // 주석을 해제하여 ADC DMA 테스트를 실행할 수 있습니다
    // start_adc_dma_test();
    
    // 실시간 ADC 모니터링 시작 (선택사항)
    // 주석을 해제하여 실시간 모니터링을 실행할 수 있습니다
    // start_adc_monitor();
    
    
    // 메인 루프
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Small Oscilloscope running - check serial output for real-time data");
    }
} 