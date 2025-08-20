# small_oscilloscope
480x272 small-but-full oscilloscope

# 오실로스코프 만들기

## 개발 환경 설정

### ESP-IDF 설치 (Windows)

1. **ESP-IDF 다운로드 및 설치**
   ```bash
   # Git 설치 후
   git clone --recursive https://github.com/espressif/esp-idf.git
   cd esp-idf
   install.bat
   ```

2. **환경 변수 설정**
   ```bash
   # ESP-IDF 환경 활성화
   export.bat
   ```

3. **프로젝트 빌드**
   ```bash
   cd small_oscilloscope
   idf.py build
   idf.py -p COM_PORT flash
   idf.py -p COM_PORT monitor
   ```

### 대안: ESP-IDF Windows Installer 사용
- [ESP-IDF Windows Installer](https://dl.espressif.com/dl/esp-idf/) 다운로드
- 설치 후 자동으로 환경이 설정됨
## 목표
- 아날로그 2채널 입력 가능한 오실로스코프
- 디지털 입력 모드 구현 및 신호 분석이 되는 오실로스코프
- 노브 컨트롤, 로터리 인코더 컨트롤
- DSO138 오실로스코프보다 더 좋은, Proteus의 오실로스코프 같은 오실로스코프
- Battery-Powered, 충전 가능
- 속도는 ESP32 구성의 최대치 (기존 칩 쓰거나, 문제가 크면 ESP32-C6 사용)

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


- Chipset : LOLIN D32
- Display : FT800Q-R


- Analog : 
- [ ] +5V 벅부스트 제너레이터(LOLIN 내장, but 배터리 회로 제작 예정)
- [x] -5V 생성기(NE555 + 쇼트키)
- [ ] +3.3 생성기
- [ ] OPAMP
- [ ] BNC
- [x] 로터리 스위치
- [ ] +12V 생성기(음전압을 위한 생성기)


LCD : 480x272 Monitor
# pin mapping
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

|GPIO36|SENSOR_VP  |Analog Reference Input;  VBAT ref, Vin=VBAT/2|
|GPIO37|SENSOR_CAPP|Analog Reference Input;  5V ref, Vin=V5/4.333|
|GPIO38|SENSOR_CAPN|Analog Reference Input;  12V ref,Vin=V12/6.732|
|GPIO39|SENSOR_VN  |Analog Reference Input;  2.7V ref, Vin=1.35V |

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

# 아날로그 구조

[출력] - [AC/DC 선택]  -   [1차 감쇠 회로] - [2차 감쇠 회로] - 레벨 쉬프터

## AC/DC 선택
|Q1/Q7|Q2/Q8|상태|
|--|--|--|
|0|0|FLOATING|
|0|1|INVALD(DC입력)|
|1|0|DC입력(원본)|
|1|1|INVALD(DC입력)|

## 1차 감쇠기
|Q3/Q9|Q4/Q10|상태|
|--|--|--|
|0|0|감쇠 없음|
|0|1|1/100 감쇠|
|1|0|1/10 감쇠|
|1|1|INVALD|
V1 = alpha_1 * Vi

## 2차 감쇠기
|Q5/Q11|Q6/Q12|상태|
|--|--|--|
|0|0|감쇠 없음|
|0|1|1/5 감쇠|
|1|0|1/2 감쇠|
|1|1|INVALD|
V2 = alpha_2 * V1

## 레벨 쉬프터 식
Vadc = 8 * V2 + (3.3/2)

# 핀 전압 오류
| pin | Z_VCC | Z_GND |
|-----|-------|-------|
| SS | inf | inf |
| SCK | 1.5k | 43 |
| MISO | 1.5k | 74 |
| MOSI | 1.5k | 100 |

# 결론
- VCore를 그냥 3.3V에 연결해서 칩이 뜨거워졌고, 코어가 파괴된걸로 추정.
- MOSI 문제는 Logic Pirate의 문제였다
- 코어 회로 수정

### 음전압 충분하지 못한 문제
- 3.3V -> -1.7V
- 5V로 개조 -> -3.N V
- 12V 외부 변환기 입력 -> -9V, 성공적인 레귤레이션
- 해결책 : 충분한 12V대 제너레이터를 만들거나 음전압 SMPS를 만들기

### 모니터 전원부 24V 문제
- 1. 저항 잊어먹음. 무부하 24V가 연결됨
- 2. 바로 옆의 GND와 충돌.

계획
- 이번 주 내로 부품 주문, 기판 주문(Stensil)
- ESP32 모듈도 내장으로 같이 붙이기 (PICO-D4 / WROOM32) C6은 싸지만 너무 느릴듯

- 배터리 관리 회로 따로 만들기
   - 충전 회로
   - 승압 회로

# 최종 확정 사항
## 전원
- 상시 전원 공급 Source : VBAT=2.7 ~ 4.2
- BAT 충전       : 5V -> VBAT=4.2 (TP4056)
- 배터리 승압 회로 : VBAT -> 5V    (EA2208)
- 양전압 생성 회로 : VBAT -> 12V   (EA2208)
- 음전압 생성 회로 : 12V -> -12V   (LM2576)
- 백라이트 회로    : VBAT -> 24V   (EA2208)
- 강하 회로       : 5V -> 3.3V    (EA8105)

## 칩
- 아날로그 회로 테스트, 결과에 따른 아날로그 회로 확정
   - [x] 아날로그 회로 작동여부 테스트 (1시간)
   - [x] 노이즈 정도에 따라 아날로그 전원 필요 여부 판단 (30분)
- LCD, Graphic과 같이 Stensil로 기판 박제 예정
- Stensil로 하는것과 차이가 있는지 확인 필요
   - [x] 회로 만들기(3~5시간 -> 6+6시간+2시간)
      - Processor : ESP32-PICO
         - JLCPCB Stensil
      - 충전 : TP4056 + USB 입력 회로(CH340C 연결) <CH340 추가 구매 필요 okay> 
	     - USB는 기용이가 쓰는 걸로 (DPDM, VBUS 다 있는 USB-C)
      - Relay : JQ1-5V-F (5V) 
         - Relay1[NC=DC,NO=AC], Relay2[NC=/1,NO=/10]
         - 2라인 총 4개 필요
         - 나머지 1차/100, 2차/2, 2차/5는 MOSFET로. <MOSFET 추가 구매 필요 okay> 
      - LM2576 12V
      - EA2208 24V, 12V  <Inductor 추가 구매 필요 (40mOhm, ~10uH, 1.2MHz Small Loss) 24V 라인에 비해 12V 라인은 더 용량이 커야함> 
      - EA2208 배선 전 꼭 확인하기
         <1 High current path traces need to be widened.>
         <2 Place the input and output capacitors as close as possible to the device to reduce noiseinterference.>
         <3 The GND pin should be connected to a large area ground plane>
         <4 Keep the feedback path (from VOUT to FBK) away from the noise node (ex. SWITCH). >

      - 전원 회로, LCD회로, 아날로그 회로, USB 회로 작성 완료. 메인 프로세서 부분만 남음.

      - 안테나 선 두께는 60mil로 해야함. JLCPCB Impeadence 계산기로 계산하자.
      - 정합은 하지 않는 걸로.
   - [ ] <엘레파츠 필요부품 정리 및 장바구니(1시간, 배송 9일 소요)>
      -  <button/headersocket 추가 구매 필요 okay> 
   - [ ] PCB Assembly 출력파일 조절 및 부품 포맷 통일 및 배치 파일 생성(1시간)
   - [ ] JLCPCB 사이트에서 견적 비교하기 (배송 4일 소요)

   - [x] 엘레파츠 주문 및 JLCPCB 주문
   - 6/27 : 엘레파츠 35,519
   - 6/27 : JLCPCB 3,577
   - 7/18 : 엘레파츠 44,515
   - 7/19 : JLCPCB 43,199  총 소비 : 126,810

   - 8/19 : 1차 Stencil 회로 완성, ESP32-PICO-D4 인식 성공, BACKLIGHT 성공
   




## 아날로그 테스트 결과(7/15)
- CH423 Test : 성공
   - ESP-IDF의 모든 함수의 어드레스는 W/R을 제외한, 내부적으로 1칸 좌쉬프트 처리하는 어드레스이다.
   - 8-bit 기준 0x48 주소를 0x24 주소로 했더니 잘 인식했다.
   - i2c 스캐너를 활용해 칩이 인식된 것을 확인했다.
   - 0x22, 0x23 주소를 통해 OC0~16을 제어하고, 0x20에 0x49를 명령하고 바이트를 읽으면 IO0~7까지 데이터를 읽을 수 있다.(사전에 IO처리 필요)
   - analog_test_simple 85:182에 있음
- 아날로그 회로 테스트
   - -5V 라인이 불안정하다.(-4.75V)
   - 기본적으로 1차에서 1/10 감쇠되길래 확인해보니, MOSFET가 무슨 일인지 Rds(off)가 2kOhm쯤 되었다. 옆 라인의 MOSFET는 Rds(off)=146k정도가 되었고, 이는 중대한 문제다.
   - Analog Switch가 전혀 동작하지 않았다. Relay로 해야할 것 같다.
   - 1/10 감쇠 1채널만 스위칭 오류
   - AC/DC부분, 1차 증폭 부분만 릴레이를 사용하기
   - 1/10, 1/100, 1/2, 1/5 1차 2차 감쇠 모두 적용 성공
   - ADC에 CAP를 설치하거나 ADC 칩을 달아야 할 것 같다(ESP32 내부 ADC 작동 잘 못함)
   - i2c ADC 또는 SPI ADC 설치 필요
- 아날로그 전원은 넣지 않기로 했다. 다만 vcc 라인을 좀 더 신경써서 작업해야한다. 디지털과 잡음이 생기지 않도록 갈라서 배선하고, 전원 소스를 잘 놓아야 한다.

