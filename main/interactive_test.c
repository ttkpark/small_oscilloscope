#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "ft800.h"
#include "hardware_test.h"
#include "analog_test_simple.h"

static const char *TAG = "INTERACTIVE_TEST";

// 공용 변수 - 인코더 카운터 (태스크 간 공유)
static volatile int g_re0_counter = 0;
static volatile int g_re1_counter = 0;
static volatile bool g_re0_pressed = false;
static volatile bool g_re1_pressed = false;

// 인코더 폴링 관련 변수
static int re0_last_state = 0;
static int re1_last_state = 0;

// 버튼 폴링 관련 변수
static bool button_polling_enabled = false;
static uint8_t last_button_state = 0xFF; // 초기값 (모든 버튼이 눌리지 않은 상태)

// 인코더 인터럽트 관련 변수
static QueueHandle_t encoder_queue = NULL;
static volatile bool encoder_interrupt_enabled = false;

// 인코더 인터럽트 데이터 구조체
typedef struct {
    uint8_t encoder_id;  // 0: RE0, 1: RE1
    bool direction;      // true: clockwise, false: counter-clockwise
    uint64_t timestamp;
} encoder_interrupt_data_t;

// CH423 I2C 주소 (기존 코드와 맞춤)
// CH423_I2C_ADDR는 analog_test_simple.h에서 이미 정의됨

// GPIO 핀 정의
#define GPIO_RE1B 12
#define GPIO_RE1A 13
#define GPIO_RE0B 14
#define GPIO_RE0A 15
#define GPIO_TRIG0 9
#define GPIO_TRIG1 10

// CH423 출력 핀 정의
#define CH423_OC_BACKLIGHT 1
#define CH423_OC_LED0 12
#define CH423_OC_LED1 13
#define CH423_OC_LEDRE0 14
#define CH423_OC_LEDRE1 15
#define CH423_OC_RELAY1 2
#define CH423_OC_RELAY2 3
#define CH423_OC_RELAY3 4
#define CH423_OC_RELAY4 5

#define CH423_OC_Q1 0
#define CH423_OC_Q3 2
#define CH423_OC_Q4 3
#define CH423_OC_Q5 4
#define CH423_OC_Q6 5
#define CH423_OC_Q7 6
#define CH423_OC_Q9 8
#define CH423_OC_Q10 9
#define CH423_OC_Q11 10
#define CH423_OC_Q12 11

// CH423 입력 핀 정의
#define CH423_IO_RE0P 0
#define CH423_IO_RE1P 1
#define CH423_IO_SW0 2
#define CH423_IO_SW1 3
#define CH423_IO_SW2 4
#define CH423_IO_SW3 5
#define CH423_IO_STDBY 6
#define CH423_IO_CHRG 7

// LED 제어 상태
typedef struct {
    bool led0_state;
    bool led1_state;
    bool ledre0_state;
    bool ledre1_state;
    int selected_led;  // 0-3: LED0, LED1, LEDRE0, LEDRE1
} led_control_t;

// 릴레이 제어 상태
typedef struct {
    bool relay1_state;
    bool relay2_state;
    bool relay3_state;
    bool relay4_state;
    int selected_relay;  // 0-3: RELAY1, RELAY2, RELAY3, RELAY4
    bool relay_test_mode;  // 릴레이 테스트 모드
    int gain_test_mode;  // 릴레이 테스트 모드
    int gain_state[6]; // 0번 AC/DC, 1차, 2차 | 1번 AC/DC, 1차, 2차
    int gain_selected_state; // 0-5
    int relay_blink_count;  // 점멸 카운터
} relay_control_t;

// 입력 상태 구조체
typedef struct {
    // 로터리 인코더 상태
    int re0_a;
    int re0_b;
    int re1_a;
    int re1_b;
    bool re0_pressed;
    bool re1_pressed;
    
    // 버튼 상태
    bool sw0_pressed;
    bool sw1_pressed;
    bool sw2_pressed;
    bool sw3_pressed;
    
    // 트리거 상태
    bool trig0_active;
    bool trig1_active;
    
    // 배터리 상태
    bool battery_charging;
    bool battery_standby;
    
    // 인코더 카운터
    int re0_counter;
    int re1_counter;
} input_status_t;

static led_control_t led_ctrl = {0};
static relay_control_t relay_ctrl = {0};
static input_status_t input_status = {0};


uint64_t encoder_last_time[2] = {0U,0U};
// 인코더 인터럽트 ISR 핸들러
static void IRAM_ATTR encoder_isr_handler(void* arg) {
    if (!encoder_interrupt_enabled) return;
    
    // 어떤 인코더에서 인터럽트가 발생했는지 확인
    uint32_t gpio_num = (uint32_t)arg;
    encoder_interrupt_data_t data;
    
    if (gpio_num == GPIO_RE1A && gpio_get_level(GPIO_RE1A) == 1) {
        data.encoder_id = 1; // RE1
        // RE1A 상승엣지에서 RE1B 상태 확인
        data.direction = gpio_get_level(GPIO_RE1B) == 1; // B=1이면 시계방향
    } else if (gpio_num == GPIO_RE0B && gpio_get_level(GPIO_RE0B) == 1) {
        data.encoder_id = 0; // RE0
        // RE0B 상승엣지에서 RE0A 상태 확인
        data.direction = gpio_get_level(GPIO_RE0A) == 0; // A=0이면 시계방향
    } else {
        return; // 알 수 없는 핀
    }
    
    data.timestamp = esp_timer_get_time();

    if(data.timestamp - encoder_last_time[data.encoder_id] > 500) { // 5ms 이하  차이나면 인터럽트 무시
        encoder_last_time[data.encoder_id] = data.timestamp;
    } else {
        return;
    }
    
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(encoder_queue, &data, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// 인코더 전용 태스크 (인터럽트 방식으로 변경됨)
static void encoder_task(void *pvParameters) {
    ESP_LOGI(TAG, "Encoder task started (interrupt mode)");
    
    // 초기 상태 읽기 (인터럽트 방식에서는 사용하지 않음)
    re0_last_state = (gpio_get_level(GPIO_RE0A) << 1) | gpio_get_level(GPIO_RE0B);
    re1_last_state = (gpio_get_level(GPIO_RE1A) << 1) | gpio_get_level(GPIO_RE1B);
    
    while (1) {
        // 인터럽트 방식으로 변경되었으므로 폴링 로직 제거
        // 인터럽트에서 카운터가 업데이트됨
        vTaskDelay(pdMS_TO_TICKS(100)); // 10Hz로 변경 (인터럽트 방식이므로 느려도 됨)
    }
}

// 인코더 인터럽트 처리 태스크
static void encoder_interrupt_task(void *pvParameters) {
    ESP_LOGI(TAG, "Encoder interrupt task started");
    
    encoder_interrupt_data_t data;
    
    while (1) {
        if (xQueueReceive(encoder_queue, &data, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Encoder interrupt: id=%d, dir=%s, time=%llu", 
                     data.encoder_id, data.direction ? "CW" : "CCW", data.timestamp);
            
            // 인코더 카운터 업데이트
            if (data.encoder_id == 0) { // RE0
                if (data.direction) {
                    g_re0_counter++;
                } else {
                    g_re0_counter--;
                }
            } else if (data.encoder_id == 1) { // RE1
                if (data.direction) {
                    g_re1_counter++;
                } else {
                    g_re1_counter--;
                }
            }
        }
    }
}


// 인코더 인터럽트 설정
static esp_err_t setup_encoder_interrupts(void) {
    // 인코더 인터럽트 큐 생성
    encoder_queue = xQueueCreate(20, sizeof(encoder_interrupt_data_t));
    if (encoder_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create encoder queue");
        return ESP_FAIL;
    }
    
    // 인코더 A 핀들을 상승엣지 인터럽트로 설정
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_RE1A),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE, // 상승엣지
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config 1 failed: %s", esp_err_to_name(ret));
        return ret;
    }
    // 인코더 A 핀들을 상승엣지 인터럽트로 설정
    io_conf.pin_bit_mask = (1ULL << GPIO_RE0B);
    io_conf.intr_type = GPIO_INTR_POSEDGE; // 상승엣지
    
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config 2 failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 인터럽트 핸들러 설치 (이미 설치되어 있을 수 있음)
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "GPIO ISR service install failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // RE1A 인터럽트 핸들러 추가
    ret = gpio_isr_handler_add(GPIO_RE1A, encoder_isr_handler, (void*)GPIO_RE1A);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO RE1A ISR handler add failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // RE0B 인터럽트 핸들러 추가
    ret = gpio_isr_handler_add(GPIO_RE0B, encoder_isr_handler, (void*)GPIO_RE0B);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO RE0B ISR handler add failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    encoder_interrupt_enabled = true;
    ESP_LOGI(TAG, "Encoder interrupts configured successfully");
    return ESP_OK;
}

// 입력 상태 업데이트
static void update_input_status(void) {
    // 로터리 인코더 상태 읽기
    input_status.re0_a = gpio_get_level(GPIO_RE0A);
    input_status.re0_b = gpio_get_level(GPIO_RE0B);
    input_status.re1_a = gpio_get_level(GPIO_RE1A);
    input_status.re1_b = gpio_get_level(GPIO_RE1B);
    
    // 공용 변수에서 인코더 카운터 가져오기
    input_status.re0_counter = g_re0_counter;
    input_status.re1_counter = g_re1_counter;
    
    // 트리거 상태 읽기
    input_status.trig0_active = !gpio_get_level(GPIO_TRIG0); // Active LOW
    input_status.trig1_active = !gpio_get_level(GPIO_TRIG1); // Active LOW
    
    // CH423 입력 읽기 (배터리 상태만 - 버튼은 폴링 태스크에서 처리)
    uint8_t input_data;
    esp_err_t ret = ch423_read_input(&input_data);
    if (ret == ESP_OK) {
        // 배터리 상태만 읽기
        input_status.battery_charging = (input_data & (1 << CH423_IO_CHRG)) != 0;
        input_status.battery_standby = (input_data & (1 << CH423_IO_STDBY)) != 0;
        
        // 버튼 상태를 여기서 직접 관리
        static bool prev_sw0 = false, prev_sw1 = false, prev_sw2 = false, prev_sw3 = false;
        static bool prev_re0 = false, prev_re1 = false;

        // 버튼 입력 비트 추출 (SW0~SW3: 비트 2~5, active low)
        bool sw0 = !(input_data & (1 << 2));
        bool sw1 = !(input_data & (1 << 3));
        bool sw2 = !(input_data & (1 << 4));
        bool sw3 = !(input_data & (1 << 5));

        bool re0 = !(input_data & (1 << 0));
        bool re1 = !(input_data & (1 << 1));

        // 버튼이 새로 눌렸을 때만 true로 설정 (rising edge)
        input_status.sw0_pressed = (sw0 && !prev_sw0);
        input_status.sw1_pressed = (sw1 && !prev_sw1);
        input_status.sw2_pressed = (sw2 && !prev_sw2);
        input_status.sw3_pressed = (sw3 && !prev_sw3);

        // 이전 상태 저장
        prev_sw0 = sw0;
        prev_sw1 = sw1;
        prev_sw2 = sw2;
        prev_sw3 = sw3;

        input_status.re0_pressed = (re0 && !prev_re0);
        input_status.re1_pressed = (re1 && !prev_re1);

        prev_re0 = re0;
        prev_re1 = re1;
    } else {
        // I2C 실패 시 기본값 설정
        input_status.sw0_pressed = false;
        input_status.sw1_pressed = false;
        input_status.sw2_pressed = false;
        input_status.sw3_pressed = false;
        input_status.re0_pressed = false;
        input_status.re1_pressed = false;
        input_status.battery_charging = false;
        input_status.battery_standby = false;
    }
}

// 인코더 카운터 리셋 (LED 제어 후 호출)
static void reset_encoder_counters(void) {
    g_re0_counter = 0;
    g_re1_counter = 0;
}

static void gain_test_mode_update(int* gain_state){


    switch(gain_state[0]){
        case 0://AC - 현재는 CONNECT (AC/DC와 관계없음)
            ch423_set_output(CH423_OC_Q1, false);
            break;
        case 1://DC - 현재는 NO_CONECT (AC/DC와 관계없음)
            ch423_set_output(CH423_OC_Q1, true);
            break;
    }

    switch(gain_state[1]){
        case 0: // 1차 1x
            ch423_set_output(CH423_OC_Q3, false);
            ch423_set_output(CH423_OC_Q4, false);
            break;
        case 1: // 1차 1/10x
            ch423_set_output(CH423_OC_Q3, true);
            ch423_set_output(CH423_OC_Q4, false);
            break;
        case 2: // 1차 1/91.9x
            ch423_set_output(CH423_OC_Q3, false);
            ch423_set_output(CH423_OC_Q4, true);
            break;
        case 3: // 1차 invalid (1/101x)
            ch423_set_output(CH423_OC_Q3, true);
            ch423_set_output(CH423_OC_Q4, true);
            break;
    }

    switch(gain_state[2]){
        case 0: // 2차 1x
            ch423_set_output(CH423_OC_Q6, false);
            ch423_set_output(CH423_OC_Q5, false);
            break;
        case 1: // 2차 1/2x
            ch423_set_output(CH423_OC_Q6, true);
            ch423_set_output(CH423_OC_Q5, false);
            break;
        case 2: // 2차 1/5x
            ch423_set_output(CH423_OC_Q6, false);
            ch423_set_output(CH423_OC_Q5, true);
            break;
        case 3: // 2차 invalid (1/6x)
            ch423_set_output(CH423_OC_Q6, true);
            ch423_set_output(CH423_OC_Q5, true);
            break;
    }
    
    switch(gain_state[3]){
        case 0://AC - 현재는 CONNECT (AC/DC와 관계없음)
            ch423_set_output(CH423_OC_Q7, false);
            break;
        case 1://DC - 현재는 NO_CONECT (AC/DC와 관계없음)
            ch423_set_output(CH423_OC_Q7, true);
            break;
    }

    switch(gain_state[4]){
        case 0: // 1차 1x
            ch423_set_output(CH423_OC_Q9, false);
            ch423_set_output(CH423_OC_Q10, false);
            break;
        case 1: // 1차 1/10x
            ch423_set_output(CH423_OC_Q9, true);
            ch423_set_output(CH423_OC_Q10, false);
            break;
        case 2: // 1차 1/91.9x
            ch423_set_output(CH423_OC_Q9, false);
            ch423_set_output(CH423_OC_Q10, true);
            break;
        case 3: // 1차 invalid (1/101x)
            ch423_set_output(CH423_OC_Q9, true);
            ch423_set_output(CH423_OC_Q10, true);
            break;
    }

    switch(gain_state[5]){
        case 0: // 2차 1x
            ch423_set_output(CH423_OC_Q12, false);
            ch423_set_output(CH423_OC_Q11, false);
            break;
        case 1: // 2차 1/2x
            ch423_set_output(CH423_OC_Q12, true);
            ch423_set_output(CH423_OC_Q11, false);
            break;
        case 2: // 2차 1/5x
            ch423_set_output(CH423_OC_Q12, false);
            ch423_set_output(CH423_OC_Q11, true);
            break;
        case 3: // 2차 invalid (1/6x)
            ch423_set_output(CH423_OC_Q12, true);
            ch423_set_output(CH423_OC_Q11, true);
            break;
    }
}

// LED 상태 업데이트 (공용 변수 사용)
static void update_led_states(void) {
    if(relay_ctrl.gain_test_mode || relay_ctrl.relay_test_mode)return;
    // RE0으로 LED 선택 변경
    if (input_status.re0_counter != 0) {
        int change = input_status.re0_counter > 0 ? 1 : -1;

        led_ctrl.selected_led = (led_ctrl.selected_led + change + 4) % 4;
        
        reset_encoder_counters(); // 공용 변수 카운터 리셋
    }
    
    // RE1으로 선택된 LED 토글
    if (input_status.re1_counter != 0) {
        int change = input_status.re1_counter > 0 ? 1 : -1;

        if (change > 0) { // 시계방향으로 회전하면 LED 토글
            switch (led_ctrl.selected_led) {
                case 0: led_ctrl.led0_state = !led_ctrl.led0_state; break;
                case 1: led_ctrl.led1_state = !led_ctrl.led1_state; break;
                case 2: led_ctrl.ledre0_state = !led_ctrl.ledre0_state; break;
                case 3: led_ctrl.ledre1_state = !led_ctrl.ledre1_state; break;
            }
        }
        reset_encoder_counters(); // 공용 변수 카운터 리셋
    }
    
    // LED 상태를 CH423에 적용 (실패해도 계속 진행)
    ch423_set_output(CH423_OC_LED0, led_ctrl.led0_state);
    ch423_set_output(CH423_OC_LED1, led_ctrl.led1_state);
    ch423_set_output(CH423_OC_LEDRE0, led_ctrl.ledre0_state);
    ch423_set_output(CH423_OC_LEDRE1, led_ctrl.ledre1_state);
}

// gain test 관련 컨트롤 함수 (공용 변수 사용)
static void update_gain_test(void) {
    if(!relay_ctrl.gain_test_mode)return;

    // RE0으로 LED 선택 변경
    if (input_status.re0_counter != 0) {
        int change = input_status.re0_counter > 0 ? 1 : -1;

        if(relay_ctrl.gain_selected_state%3==0){
            relay_ctrl.gain_state[relay_ctrl.gain_selected_state] = (relay_ctrl.gain_state[relay_ctrl.gain_selected_state] + change + 2) % 2;
    
        }else{
            relay_ctrl.gain_state[relay_ctrl.gain_selected_state] = (relay_ctrl.gain_state[relay_ctrl.gain_selected_state] + change + 4) % 4;
        }
        reset_encoder_counters(); // 공용 변수 카운터 리셋
    }
    
    // RE1으로 선택된 LED 토글
    if (input_status.re1_counter != 0) {
        int change = input_status.re1_counter > 0 ? 1 : -1;

        relay_ctrl.gain_selected_state = (relay_ctrl.gain_selected_state+change+6)%6;

        reset_encoder_counters(); // 공용 변수 카운터 리셋
    }
    gain_test_mode_update(relay_ctrl.gain_state);
    
}

// 릴레이 상태 업데이트
static void update_relay_states(void) {
    if(!relay_ctrl.relay_test_mode)return;
    // 릴레이 테스트 모드에서 점멸
    if (relay_ctrl.relay_test_mode) {
        relay_ctrl.relay_blink_count++;
        if (relay_ctrl.relay_blink_count >= 10) { // 1초마다 토글 (10 * 100ms)
            relay_ctrl.relay1_state = !relay_ctrl.relay1_state;
            relay_ctrl.relay2_state = !relay_ctrl.relay2_state;
            relay_ctrl.relay3_state = !relay_ctrl.relay3_state;
            relay_ctrl.relay4_state = !relay_ctrl.relay4_state;
            relay_ctrl.relay_blink_count = 0;
        }
    }
    
    // 릴레이 상태를 CH423에 적용
    ch423_set_output(CH423_OC_RELAY1, relay_ctrl.relay1_state);
    ch423_set_output(CH423_OC_RELAY2, relay_ctrl.relay2_state);
    ch423_set_output(CH423_OC_RELAY3, relay_ctrl.relay3_state);
    ch423_set_output(CH423_OC_RELAY4, relay_ctrl.relay4_state);
}

// UI 그리기
static void draw_ui(void) {
    char status_text[200];
    
    int y = 0, inc = 15;
	cmd(CMD_DLSTART);
    // 배경색 설정
    cmd(COLOR_RGB(0x20, 0x20, 0x20));
    cmd(CLEAR(1, 1, 1));
    
    cmd(COLOR_RGB(0xFF, 0xFF, 0xFF));
    // ADC 값 표시
    sprintf(status_text, "ADC1: %d, ADC2: %d", get_adc_latest_value1(), get_adc_latest_value2());
    cmd_text(10, y, 20, 0, status_text);
    y+=inc;
    // ADC 그래프 그리기
    cmd(COLOR_RGB(0x00, 0xFF, 0x00)); // 초록색 (ADC1)
    cmd(BEGIN(LINES));
    uint16_t* adc_buffer = get_adc_buffer();
    int buffer_index = get_adc_buffer_index();
    int graph_width = 250;
    int graph_height = 180;
    int graph_x = 200;
    int graph_y = 25;
    

    // ADC1 그래프 (초록색)
    for (int i = 0; i < graph_width; i+=3) {
        int buffer_pos = (buffer_index - graph_width + i + 256) % 256;
        int adc_value = adc_buffer[buffer_pos];
        int y_pos = graph_y + graph_height - (adc_value * graph_height / 4096);
        
        if (i > 0) {
            int prev_buffer_pos = (buffer_index - graph_width + i - 3 + 256) % 256;
            int prev_adc_value = adc_buffer[prev_buffer_pos];
            int prev_y_pos = graph_y + graph_height - (prev_adc_value * graph_height / 4096);
            
            cmd(VERTEX2F((graph_x + i - 3) * 16, prev_y_pos * 16));
            cmd(VERTEX2F((graph_x + i) * 16, y_pos * 16));
        }
    }
    cmd(END());
    
    
    // ADC2 그래프 (파란색)
    cmd(COLOR_RGB(0x00, 0x00, 0xFF));
    cmd(BEGIN(LINES));
    for (int i = 0; i < graph_width; i+=3) {
        int buffer_pos = (buffer_index - graph_width + i + 256) % 256;
        int adc_value = adc_buffer[buffer_pos + 256]; // ADC2는 버퍼의 후반부
        int y_pos = graph_y + graph_height - (adc_value * graph_height / 4096);
        
        if (i > 0) {
            int prev_buffer_pos = (buffer_index - graph_width + i - 3 + 256) % 256;
            int prev_adc_value = adc_buffer[prev_buffer_pos + 256];
            int prev_y_pos = graph_y + graph_height - (prev_adc_value * graph_height / 4096);
            
            cmd(VERTEX2F((graph_x + i - 3) * 16, prev_y_pos * 16));
            cmd(VERTEX2F((graph_x + i) * 16, y_pos * 16));
        }
    }
    cmd(END());

    // 그래프 둘레에 흰색 선 그리기
    cmd(COLOR_RGB(0xFF, 0xFF, 0xFF));
    cmd(BEGIN(LINES));
    // 왼쪽 세로선
    cmd(VERTEX2F(graph_x * 16, graph_y * 16));
    cmd(VERTEX2F(graph_x * 16, (graph_y + graph_height) * 16));
    // 오른쪽 세로선
    cmd(VERTEX2F((graph_x + graph_width) * 16, graph_y * 16));
    cmd(VERTEX2F((graph_x + graph_width) * 16, (graph_y + graph_height) * 16));
    // 위쪽 가로선
    cmd(VERTEX2F(graph_x * 16, graph_y * 16));
    cmd(VERTEX2F((graph_x + graph_width) * 16, graph_y * 16));
    // 아래쪽 가로선
    cmd(VERTEX2F(graph_x * 16, (graph_y + graph_height) * 16));
    cmd(VERTEX2F((graph_x + graph_width) * 16, (graph_y + graph_height) * 16));
    cmd(END());


    // 제목
    cmd(COLOR_RGB(0xFF, 0xFF, 0xFF));
    cmd_text(320, 10, 28, OPT_CENTER, "Interactive Hardware Test");
    y+=inc/2;
    // 구분선
    cmd(COLOR_RGB(0x40, 0x40, 0x40));
    // LINE 함수 대신 사각형으로 구분선 그리기
    cmd(BEGIN(LINES));
    cmd(VERTEX2F(10 * 16, y * 16));
    cmd(VERTEX2F(470 * 16, y * 16));
    cmd(END());
    
    // 로터리 인코더 상태
    cmd(COLOR_RGB(0xFF, 0xFF, 0xFF));
    
    sprintf(status_text, "RE0: A=%d B=%d Pressed=%s Counter=%d", 
            input_status.re0_a, input_status.re0_b, 
            input_status.re0_pressed ? "YES" : "NO", 
            input_status.re0_counter);
    cmd_text(10, y, 20, 0, status_text);
    y+=inc;
    
    sprintf(status_text, "RE1: A=%d B=%d Pressed=%s Counter=%d", 
            input_status.re1_a, input_status.re1_b, 
            input_status.re1_pressed ? "YES" : "NO", 
            input_status.re1_counter);
    cmd_text(10, y, 20, 0, status_text);
    y+=inc;
    
        // 버튼 상태
    sprintf(status_text, "SW0=%s SW1=%s SW2=%s SW3=%s", 
            input_status.sw0_pressed ? "HI" : "LO",
            input_status.sw1_pressed ? "HI" : "LO",
            input_status.sw2_pressed ? "HI" : "LO",
            input_status.sw3_pressed ? "HI" : "LO");
    cmd_text(10, y, 20, 0, status_text);
    y+=inc;
    
    // 트리거 상태
    sprintf(status_text, "TRIG0=%s TRIG1=%s", 
            input_status.trig0_active ? "ACTIVE" : "INACTIVE",
            input_status.trig1_active ? "ACTIVE" : "INACTIVE");
    cmd_text(10, y, 20, 0, status_text);
    y+=inc;
    
    // 배터리 상태
    sprintf(status_text, "Battery: Charging=%s Standby=%s", 
            input_status.battery_charging ? "YES" : "NO",
            input_status.battery_standby ? "YES" : "NO");
    cmd_text(10, y, 20, 0, status_text);
    y+=inc;
    
    y+=inc/2;

    if(relay_ctrl.gain_test_mode){
        // LED 제어 섹션
        cmd(COLOR_RGB(0x00, 0xFF, 0x00));
        cmd_text(10, y, 24, 0, "GAIN CONTROL:");
        y+=inc*2;
        
        // LED 상태 표시
        cmd(COLOR_RGB(0xFF, 0xFF, 0xFF));
        sprintf(status_text, "GAIN MODE : %d, focus : %d, val : %d", relay_ctrl.gain_test_mode, relay_ctrl.gain_selected_state, relay_ctrl.gain_state[relay_ctrl.gain_selected_state]);
        cmd_text(10, y, 20, 0, status_text);
        y+=inc;

        char *acdc_gate_txt[2] = {"AC", "DC"};
        char *first_gate_txt[4] = {"1/1x  ", "1/10x ", "1/92x ", "1/101x"};
        char *second_gate_txt[4] = {"1/1x", "1/2x", "1/5x", "1/6x"};

        sprintf(status_text, "ch0: [%d,%d,%d] %s %s %s", relay_ctrl.gain_state[0], relay_ctrl.gain_state[1], relay_ctrl.gain_state[2]
            , acdc_gate_txt[relay_ctrl.gain_state[0]], first_gate_txt[relay_ctrl.gain_state[1]], second_gate_txt[relay_ctrl.gain_state[2]]);
        cmd_text(10, y, 20, 0, status_text);
        y+=inc;

        sprintf(status_text, "ch1: [%d,%d,%d] %s %s %s", relay_ctrl.gain_state[3], relay_ctrl.gain_state[4], relay_ctrl.gain_state[5]
            , acdc_gate_txt[relay_ctrl.gain_state[3]], first_gate_txt[relay_ctrl.gain_state[4]], second_gate_txt[relay_ctrl.gain_state[5]]);
        cmd_text(10, y, 20, 0, status_text);
        y+=inc;
    }else if(!relay_ctrl.relay_test_mode){
        // LED 제어 섹션
        cmd(COLOR_RGB(0x00, 0xFF, 0x00));
        cmd_text(10, y, 24, 0, "LED CONTROL:");
        y+=inc*2;
        
        // 선택된 LED 표시
        const char* led_names[] = {"LED0", "LED1", "LEDRE0", "LEDRE1"};
        cmd(COLOR_RGB(0xFF, 0xFF, 0x00));
        sprintf(status_text, "Selected: %s", led_names[led_ctrl.selected_led]);
        cmd_text(10, y, 22, 0, status_text);
        y+=inc;
        
        // LED 상태 표시
        cmd(COLOR_RGB(0xFF, 0xFF, 0xFF));
        sprintf(status_text, "LED0: %s  LED1: %s", 
                led_ctrl.led0_state ? "HI" : "LO",
                led_ctrl.led1_state ? "HI" : "LO");
        cmd_text(10, y, 20, 0, status_text);
        y+=inc;
        
        sprintf(status_text, "LEDRE0: %s  LEDRE1: %s", 
                led_ctrl.ledre0_state ? "HI" : "LO",
                led_ctrl.ledre1_state ? "HI" : "LO");
        cmd_text(10, y, 20, 0, status_text);
        y+=inc;
    }else{
        // 릴레이 제어 섹션
        cmd(COLOR_RGB(0x00, 0xFF, 0x00));
        cmd_text(10, y, 24, 0, "RELAY CONTROL:");
        y+=inc*2;
        
        // 릴레이 상태 표시
        cmd(COLOR_RGB(0xFF, 0xFF, 0xFF));
        sprintf(status_text, "RELAY1: %s  RELAY2: %s", 
                relay_ctrl.relay1_state ? "HI" : "LO",
                relay_ctrl.relay2_state ? "HI" : "LO");
        cmd_text(10, y, 20, 0, status_text);
        y+=inc;
        
        sprintf(status_text, "RELAY3: %s  RELAY4: %s", 
                relay_ctrl.relay3_state ? "HI" : "LO",
                relay_ctrl.relay4_state ? "HI" : "LO");
        cmd_text(10, y, 20, 0, status_text);
        y+=inc;
        
        // 릴레이 테스트 모드 상태
        cmd(COLOR_RGB(0xFF, 0xFF, 0x00));
        sprintf(status_text, "Relay Test Mode: %s", 
                relay_ctrl.relay_test_mode ? "ON (1Hz Blink)" : "OFF");
        cmd_text(10, y, 18, 0, status_text);
        y+=inc;

    }

    // graph 아래에 글씨 두기
    if(y<graph_height+graph_y){
        y=graph_height+graph_y+inc/2;
    }else{  
        y+=inc/2;
    }
    
    // 조작법 안내
    cmd(COLOR_RGB(0x00, 0xFF, 0xFF));
    if(!relay_ctrl.gain_test_mode && !relay_ctrl.relay_test_mode){
        cmd_text(10, y, 18, 0, "RE0: Select LED, RE1: Toggle LED");
        y+=inc;
    }else if(relay_ctrl.gain_test_mode){
        cmd_text(10, y, 18, 0, "RE0: Modify Gain State, RE1: Select Gain State");
        y+=inc;
    }else if(relay_ctrl.relay_test_mode){
        cmd_text(10, y, 18, 0, "");
        y+=inc;
    }
    cmd_text(10, y, 18, 0, "SW0: Toggle Relay Mode, SW2: Toggle Gain Mode");
    y+=inc;
    
    // 선택된 LED 하이라이트
    int led_x = 10 + (led_ctrl.selected_led * 120);
    int led_y = y;
    cmd(COLOR_RGB(0xFF, 0x00, 0x00));
    cmd(BEGIN(LINES));
    cmd(VERTEX2F(led_x * 16, (led_y + 25) * 16));
    cmd(VERTEX2F((led_x + 100) * 16, (led_y + 25) * 16));
    cmd(END());
    
    // 화면 업데이트
    cmd(DISPLAY());
    cmd(CMD_SWAP);
}



// 실시간 상호작용 테스트 태스크
static void interactive_test_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting interactive hardware test...");
    
    // I2C가 이미 초기화되어 있는지 확인하고 대기
    vTaskDelay(pdMS_TO_TICKS(2000)); // 기존 코드의 I2C 초기화 완료 대기
    
    // 인코더 인터럽트 설정 및 태스크 시작
    esp_err_t ret = setup_encoder_interrupts();
    if (ret == ESP_OK) {
        xTaskCreate(encoder_interrupt_task, "encoder_interrupt", 4096, NULL, 8, NULL);
        ESP_LOGI(TAG, "Encoder interrupt mode enabled");
    } else {
        ESP_LOGW(TAG, "Encoder interrupt setup failed, using polling mode");
        xTaskCreate(encoder_task, "encoder_task", 4096, NULL, 8, NULL);
    }
    
    // 백라이트 ON
    ch423_set_output(CH423_OC_BACKLIGHT, false);
    /*
    // FT800 초기화
    if (initFT800() != 0) {
        ESP_LOGE(TAG, "FT800 init failed");
        vTaskDelete(NULL);
        return;
    }*/
    
    // 초기 LED 상태 설정
    led_ctrl.selected_led = 0;
    led_ctrl.led0_state = false;
    led_ctrl.led1_state = false;
    led_ctrl.ledre0_state = false;
    led_ctrl.ledre1_state = false;
    
    // 초기 입력 상태 초기화
    memset(&input_status, 0, sizeof(input_status));
    
    ESP_LOGI(TAG, "Interactive test started - use rotary encoders to control LEDs");
    
    // 메인 루프
    while (1) {
        // 입력 상태 업데이트 (공용 변수에서 인코더 값 가져오기)
        update_input_status();
        
        // 버튼 0이 눌렸을 때 relay_test_mode 토글
        if (input_status.sw0_pressed) {
            relay_ctrl.relay_test_mode = !relay_ctrl.relay_test_mode;
            relay_ctrl.relay_blink_count = 0; // 점멸 카운터 리셋
            ESP_LOGI(TAG, "SW0 pressed: Relay test mode %s", 
                     relay_ctrl.relay_test_mode ? "ENABLED" : "DISABLED");
            
            // 릴레이 테스트 모드가 꺼졌을 때 모든 릴레이 OFF
            if (!relay_ctrl.relay_test_mode) {
                relay_ctrl.relay1_state = false;
                relay_ctrl.relay2_state = false;
                relay_ctrl.relay3_state = false;
                relay_ctrl.relay4_state = false;
            }
        }
        
        // 버튼 1이 눌렸을 때 모드 적용
        if (input_status.sw2_pressed) {
            ESP_LOGI(TAG, "SW2 pressed: Gain test mode %s", 
                     relay_ctrl.gain_test_mode ? "ENABLED" : "DISABLED");
            
            if(relay_ctrl.relay_test_mode){
                relay_ctrl.relay_test_mode = false;
            }
            // 릴레이 테스트 모드가 꺼졌을 때 모든 릴레이 OFF
            if (relay_ctrl.gain_test_mode) {
                
            }

            relay_ctrl.gain_test_mode = !relay_ctrl.gain_test_mode;
        }
        
        // LED 상태 업데이트
        update_led_states();
        
        // 릴레이 상태 업데이트
        update_relay_states();

        // gain 상태 업데이트
        update_gain_test();
        
        // UI 그리기
        draw_ui();
        
        // 100ms 대기 (10 FPS) - 깜빡임 줄이기
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// 실시간 상호작용 테스트 시작
esp_err_t start_interactive_test(void) {
    xTaskCreate(interactive_test_task, "interactive_test", 8192, NULL, 5, NULL);
    return ESP_OK;
}
