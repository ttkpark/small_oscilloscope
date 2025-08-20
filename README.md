# small_oscilloscope
480x272 small-but-full oscilloscope


## stpes to go

### 아날로그 회로
 - 오실로스코프 회로 방식 파악 필요
 - DSO138의 회로 구성을 확인

### 오실로스코프 UI
 - 제어점과 보여줄 점을 파악 필요
 - `사용자의 입력은 무엇이 되어야 하는가`
 - `오실로스코프의 아날로그 유닛에 대한 제어 범위는?`
 - `어느 점을 보여주어야 하는가`

 - 트리거 인식 : 샘플 내에서 level지점 통과 지점을 지점 정중앙으로 둔다.
 - 샘플링 후 트리거 인식이 아니라, 샘플링 하면서 트리거 인식하면 트리거 지점이 정중앙되도록 샘플 수집하고 마무리.
 - 즉 샘플링 -> 트리거 x, 트리거 -> 샘플링 o


## 전원
- 상시 전원 공급 Source : VBAT=2.7 ~ 4.2
- BAT 충전       : 5V -> VBAT=4.2 (TP4056)
- 배터리 승압 회로 : VBAT -> 5V    (EA2208)
- 양전압 생성 회로 : VBAT -> 12V   (EA2208)
- 음전압 생성 회로 : 12V -> -12V   (LM2576)
- 백라이트 회로    : VBAT -> 24V   (EA2208)
- 강하 회로       : 5V -> 3.3V    (5V->3.3V 전압 강하 보드)

## 구성요소
- 본체 중앙처리장치
   - 구성 부품 : ESP32-PICO-D4
   - 인터페이스 : GPIO, SPI, I2C, ADC, UART, DAC 제공

- GPIO 확장 칩 : CH423
   - 구성 부품 : CH423
   - 인터페이스 : 본체 인터페이스 I2C 사용
               입출력 GPIO IO0-7, 출력 GPIO OC0-15 제공

- 아날로그 어레이
   - 구성 부품 : 전압 강하 OP-AMP, 감쇠율 조절 FET 및 릴레이, AC/DC 선택 릴레이, TRIG 생성기
   - 인터페이스 : GPIO 확장 칩 OC0,OC2-6,OC8-11 사용
                본체 인터페이스 GPIO(GPIO9-10), ADC(GPIO34, GPIO4), DAC(GPIO25, GPIO26) 사용
- 그래픽
   - 구성 부품 : FT800Q-T, 480x272 Monitor
   - 인터페이스 : 본체 인터페이스 SPI, 본체 GPIO (GPIO27:INT) 사용

- 조작부
   - 구성 부품 : 버튼, ROTARY Encoder, LED
   - 인터페이스 : GPIO 확장 칩 IO0-5, OC12-15 사용

- 연결부
   - 구성 부품 : CH340, BOOT Mode selecting MOSFET
   - 인터페이스 : 본체 인터페이스 UART, 본체 GPIO 부팅 핀(GPIO0, EN) 사용

- 충전부
   - 구성 부품 : 배터리 및 TP4056
   - 인터페이스 : GPIO 확장 칩 IO6-7 사용

- 전원부
   - 구성 부품 : 레퍼런스 전압을 나누는 Voltage Divisor
   - 인터페이스 : 본체 ADC 레퍼런스 전압 포트(GPIO35-39)

# 핀맵
|position|name|usage|
|----|----|----|
|UART(TX=GPIO1,RX=GPIO3)|CH340|Interface; uploading & debugging port|
|I2C(CK=GPIO21,DA=GPIO22)|CH423s|Interface; controlls the MOSFETs|
|SPI(SCK=GPIO18,MISO=GPIO19,MOSI=GPIO23,SS=GPIO5)|FT800Q-T|Interface; graphical controller, speaker enabled, DE Mode|
|GPIO27|/INT|LCD FT800Q-T Interrupt Pin|

|GPIO12|RE1B|Digital Input; Rotary Encoder 1(Volt Div/Pos) B|
|GPIO13|RE1A|Digital Input; Rotary Encoder 1(Volt Div/Pos) A|
|GPIO14|RE0B|Digital Input; Rotray Encoder 0(Time Div/Pos) B|
|GPIO15|RE0A|Digital Input; Rotray Encoder 0(Time Div/Pos) A|

|GPIO9|TRIG0|Digital Input, interrupt; open-drain, LOW if TRIG<ADC0|
|GPIO10|TRIG1|Digital Input, interrupt; open-drain, LOW if TRIG<ADC1|
|GPIO25|TRIG|Analog Output;  Trigger Output(same level with ADC0,1 pin)|
|GPIO34|ADC0|Analog Input;  Channel 0 input|
|GPIO4|ADC1|Analog Input;  Channel 1 input|
|GPIO26|TESTPIN|1kHz Signal Output pin|

|GPIO35|VREF3V3|Analog Reference Input;  3V3(VDD) ref, Vin=V33/2|
|GPIO36|VREFBAT|Analog Reference Input;  VBAT ref, Vin=VBAT/2|
|GPIO37|VREF5V |Analog Reference Input;  5V ref, Vin=V5/4.333|
|GPIO38|VREF12V|Analog Reference Input;  12V ref,Vin=V12/6.732|
|GPIO39|VREF135|Analog Reference Input;  2.7V ref, Vin=1.35V |

|CH423s-IO0|RE0P|Input; Rotary Encoder 0 Pushbutton(LOW Active)|
|CH423s-IO1|RE1P|Input; Rotary Encoder 1 Pushbutton(LOW Active)|
|CH423s-IO2|SW0|Input; button 0(LOW ACTIVE)|
|CH423s-IO3|SW1|Input; button 1(LOW ACTIVE)|
|CH423s-IO4|SW2|Input; button 2(LOW ACTIVE)|
|CH423s-IO5|SW3|Input; button 3(LOW ACTIVE)|
|CH423s-IO6|STDBY|Input; BATTERY 충전 완료면 HIGH|
|CH423s-IO7|CHRG|Input; BATTERY 충전 중이면 HIGH|
|CH423s-OC0|Q1|Output; Line 0 DC input analog switch|
|CH423s-OC1|BACKLIGHT|Output; LCD BACKLIGHT|
|CH423s-OC2|Q3|Output; Line 0 1차 1/10 감쇠|
|CH423s-OC3|Q4|Output; Line 0 1차 1/100 감쇠|
|CH423s-OC4|Q5|Output; Line 0 2차 1/2 감쇠|
|CH423s-OC5|Q6|Output; Line 0 2차 1/5 감쇠|
|CH423s-OC6|Q7|Output; Line 1 DC input analog switch|
|CH423s-OC7|-|Output; -|
|CH423s-OC8|Q9|Output; Line 1 1차 1/10 감쇠|
|CH423s-OC9|Q10|Output; Line 1 1차 1/100 감쇠|
|CH423s-OC10|Q11|Output; Line 1 2차 1/2 감쇠|
|CH423s-OC11|Q12|Output; Line 1 2차 1/5 감쇠|
|CH423s-OC12|LED0|Output; GP LED (Red)|
|CH423s-OC13|LED1|Output; GP LED (Green)|
|CH423s-OC14|LEDRE0|Output; Rotary Encoder 0 LED (Green)|
|CH423s-OC15|LEDRE1|Output; Rotary Encoder 1 LED (Green)|



# 하드웨어 연결 및 기능 구조

## 본체 중앙처리장치
   - 구성 부품 : ESP32-PICO-D4
   - 인터페이스 : GPIO, SPI, I2C, ADC, UART, DAC 제공


## GPIO 확장 칩 : CH423
   - 구성 부품 : CH423
   - 인터페이스 : 본체 인터페이스 I2C 사용
               입출력 GPIO IO0-7, 출력 GPIO OC0-15 제공
### 인터페이스 상세
|position|name|usage|
|----|----|----|
|I2C(CK=GPIO21,DA=GPIO22)|CH423s|Interface; controlls the MOSFETs|

### 설정
- 주소는 0x20이다. (7-bit addr)
analog_test_simple.c:84 esp_err_t test_ch423_circuit_simple(uint8_t *read_data) 의 테스트 함수에서 초기화 및 읽고 쓰는 법에 대해 쓰여 있다.


## 아날로그 어레이
- 구성 부품 : 전압 강하 OP-AMP, 감쇠율 조절 FET 및 릴레이, AC/DC 선택 릴레이, TRIG 생성기
- 인터페이스 : GPIO 확장 칩 OC0,OC2-6,OC8-11 사용
             본체 인터페이스 GPIO(GPIO9-10), ADC(GPIO34, GPIO4), DAC(GPIO25, GPIO26) 사용
### 인터페이스 상세
|position|name|usage|
|----|----|----|
|GPIO9|TRIG0|Digital Input, interrupt; open-drain, LOW if TRIG<ADC0|
|GPIO10|TRIG1|Digital Input, interrupt; open-drain, LOW if TRIG<ADC1|
|GPIO25|TRIG|Analog Output;  Trigger Output(same level with ADC0,1 pin)|
|GPIO34|ADC0|Analog Input;  Channel 0 input|
|GPIO4|ADC1|Analog Input;  Channel 1 input|
|GPIO26|TESTPIN|1kHz Signal Output pin|

|CH423s-OC0|Q1|Output; Line 0 DC input analog switch|
|CH423s-OC2|Q3|Output; Line 0 1차 1/10 감쇠|
|CH423s-OC3|Q4|Output; Line 0 1차 1/100 감쇠|
|CH423s-OC4|Q5|Output; Line 0 2차 1/2 감쇠|
|CH423s-OC5|Q6|Output; Line 0 2차 1/5 감쇠|
|CH423s-OC6|Q7|Output; Line 1 DC input analog switch|
|CH423s-OC8|Q9|Output; Line 1 1차 1/10 감쇠|
|CH423s-OC9|Q10|Output; Line 1 1차 1/100 감쇠|
|CH423s-OC10|Q11|Output; Line 1 2차 1/2 감쇠|
|CH423s-OC11|Q12|Output; Line 1 2차 1/5 감쇠|

### 프로브부터 ADC 포트까지 감쇠 회로 및 전압의 관계
- [출력] - [AC/DC 선택]  -   [1차 감쇠 회로] - [2차 감쇠 회로] - 레벨 쉬프터

### 감쇠율 조정 로직 : AC/DC 선택
|Q1/Q7|상태|
|--|--|
|0|DC입력(원본)|
|1|AC입력(AC성분만)|

### 감쇠율 조정 로직 : 1차 감쇠기
|Q3/Q9|Q4/Q10|상태|
|--|--|alpha_1|
|0|0|1|
|0|1|1/100|
|1|0|1/10|
|1|1|INVALD|
- V1 = alpha_1 * Vi

### 감쇠율 조정 로직 : 2차 감쇠기
|Q5/Q11|Q6/Q12|상태|
|--|--|alpha_2|
|0|0|1|
|0|1|1/5|
|1|0|1/2|
|1|1|INVALD|
- V2 = alpha_2 * V1

### 레벨 쉬프터 식
- Vadc = 8 * V2 + (V12/7.2)

### 출력과 ADC 입력의 관계
- Vadc = 8 * alpha_2 * alpha_1 * Vi + (V12 / 7.2)
- alpha_2,alpha_1은 1차 2차 감쇠값, Vadc는 adc포트 전압, Vi는 프로브 전압, V12는 12V레인 전압

### 트리거
- TRIG0, TRIG1 핀은 pull-up되어야 한다.
|TRIG>ADCn|TRIGn|
|--|--|
|true|HIGH|
|false|LOW|


## 그래픽
   - 구성 부품 : FT800Q-T, 480x272 Monitor
   - 인터페이스 : 본체 인터페이스 SPI, 본체 GPIO (GPIO27:INT) 사용

|position|name|usage|
|----|----|----|
|SPI(SCK=GPIO18,MISO=GPIO19,MOSI=GPIO23,SS=GPIO5)|FT800Q-T|Interface; graphical controller, speaker enabled, DE Mode|
|GPIO27|/INT|LCD FT800Q-T Interrupt Pin|
|CH423s-OC1|BACKLIGHT|Output; LCD BACKLIGHT|
### 사용법
- 사전에 반드시 **CH423이 먼저 초기화되고, BACKLIGHT 핀을 LOW 해야 한다.**
- 아래 코드처럼 초기화하고, 그림을 그리고 효과음을 낸다.
```c
   // FT800 초기화
    if (!initFT800()) {
        ESP_LOGI(TAG, "FT800 test passed");
    } else {
        ESP_LOGE(TAG, "FT800 test failed");
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    spi_speedup();
    
    // FT800 사용
	 clrscr();
    while(1){
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
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    // 소리 내기
   HOST_MEM_WR8(REG_VOL_SOUND, 0xFF);      	
   switch((frames%180)/60){
      case 0:
         HOST_MEM_WR16(REG_SOUND, 0x50);     
         break;
      case 1:
         HOST_MEM_WR16(REG_SOUND, 0x51);
         break;
      case 2:
         HOST_MEM_WR16(REG_SOUND, 0x56);
         break;
   }
   HOST_MEM_WR8(REG_PLAY, 1); 
   
```

## 조작부
   - 구성 부품 : 버튼, ROTARY Encoder, LED
   - 인터페이스 : GPIO 확장 칩 IO0-5, OC12-15 사용

|position|name|usage|
|----|----|----|
|GPIO12|RE1B|Digital Input; Rotary Encoder 1(Volt Div/Pos) B|
|GPIO13|RE1A|Digital Input; Rotary Encoder 1(Volt Div/Pos) A|
|GPIO14|RE0B|Digital Input; Rotray Encoder 0(Time Div/Pos) B|
|GPIO15|RE0A|Digital Input; Rotray Encoder 0(Time Div/Pos) A|
|CH423s-IO0|RE0P|Input; Rotary Encoder 0 Pushbutton(LOW Active)|
|CH423s-IO1|RE1P|Input; Rotary Encoder 1 Pushbutton(LOW Active)|
|CH423s-IO2|SW0|Input; button 0(LOW ACTIVE)|
|CH423s-IO3|SW1|Input; button 1(LOW ACTIVE)|
|CH423s-IO4|SW2|Input; button 2(LOW ACTIVE)|
|CH423s-IO5|SW3|Input; button 3(LOW ACTIVE)|
|CH423s-OC12|LED0|Output; GP LED (Red)|
|CH423s-OC13|LED1|Output; GP LED (Green)|
|CH423s-OC14|LEDRE0|Output; Rotary Encoder 0 LED (Green)|
|CH423s-OC15|LEDRE1|Output; Rotary Encoder 1 LED (Green)|

- 로터리 스위치의 움직임 방향, 버튼의 눌림을 알 수 있고, LED 4개로 확인할 수 있다.


## 연결부
   - 구성 부품 : CH340, BOOT Mode selecting MOSFET
   - 인터페이스 : 본체 인터페이스 UART, 본체 GPIO 부팅 핀(GPIO0, EN) 사용

|position|name|usage|
|----|----|----|
|UART(TX=GPIO1,RX=GPIO3)|CH340|Interface; uploading & debugging port|

## 충전부
   - 구성 부품 : 배터리 및 TP4056
   - 인터페이스 : GPIO 확장 칩 IO6-7 사용
|position|name|usage|
|----|----|----|
|CH423s-IO6|STDBY|Input; BATTERY 충전 완료면 HIGH|
|CH423s-IO7|CHRG|Input; BATTERY 충전 중이면 HIGH|

### 충전 상태 설명
|STDBY|CHRG|상태|
|--|--|--|
|LOW |LOW |외부전원 분리됨|
|LOW |HIGH|충전 중|
|HIGH|LOW |충전 완료|
|HIGH|HIGH|INVALID, 아마도 충전중|



## 전원부
   - 구성 부품 : 레퍼런스 전압을 나누는 Voltage Divisor
   - 인터페이스 : 본체 ADC 레퍼런스 전압 포트(GPIO35-39)

### 인터페이스 상세
|position|name|usage|
|----|----|----|
|GPIO35|VREF3V3|Analog Reference Input;  3V3(VDD) ref, Vin=V33/2|
|GPIO36|VREFBAT|Analog Reference Input;  VBAT ref, Vin=VBAT/2|
|GPIO37|VREF5V |Analog Reference Input;  5V ref, Vin=V5/4.333|
|GPIO38|VREF12V|Analog Reference Input;  12V ref,Vin=V12/6.732|
|GPIO39|VREF135|Analog Reference Input;  2.7V ref, Vin=1.35V |

### 사용법
- VREF135 포트는 제너 다이오드로 연결된 거의 신뢰성 있는 전압이다. 이것을 기준으로 ADC Max Volt를 역산한다.(=VDD)
- VREFBAT의 입력을 전압으로 변환해 배터리 전압을 추출한다.
- VREF5V, 12V의 ADC입력을 전압으로 변환해 각 레인의 전압을 추출한다.
- VREF3V3은 VDD로 계산되지만, 검증용으로 예비로 전압을 추출한다.