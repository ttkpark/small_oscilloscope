#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "ft800.h"
#include "hardware_test.h"
#include "interactive_test.h"
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
    
    // 하드웨어 테스트 태스크 시작
    xTaskCreate(hardware_test_task, "hardware_test_task", 8192, NULL, 5, NULL);
    
    // 메인 루프
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Small Oscilloscope running - check serial output for real-time data");
    }
} 