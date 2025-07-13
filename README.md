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
|I2C(CK=22,DA=21)|CH423s|Interface; controlls the MOSFETs|
|SPI(SS=GPIO5)|FT800Q-T|Interface; graphical controller, speaker enabled, DE Mode|
|GPIO17|TRIG0|Digital Input, interrupt; open-drain, LOW if TRIG<ADC0|
|GPIO16|TRIG1|Digital Input, interrupt; open-drain, LOW if TRIG<ADC1|
|GPIO14|RE1A|Digital Input; Rotary Encoder 1(Volt Div/Pos) A|
|GPIO27|RE1B|Digital Input; Rotary Encoder 1(Volt Div/Pos) B|
|GPIO13|RE0A|Digital Input; Rotray Encoder 0(Time Div/Pos) A|
|GPIO12|RE0B|Digital Input; Rotray Encoder 0(Time Div/Pos) B|
|GPIO25|TRIG|Analog Output;  Trigger Output(same level with ADC0,1 pin)|
|Vp|ADC0|Analog Input;  Channel 0 input|
|Vn|ADC1|Analog Input;  Channel 1 input|
|CH423s-IO0|RE0P|Input; Rotary Encoder 0 Pushbutton(LOW Active)|
|CH423s-IO1|RE1P|Input; Rotary Encoder 1 Pushbutton(LOW Active)|
|CH423s-IO2|SW0|Input; button 0(LOW ACTIVE)|
|CH423s-IO3|SW1|Input; button 1(LOW ACTIVE)|
|CH423s-IO4|SW2|Input; button 2(LOW ACTIVE)|
|CH423s-IO5|SW3|Input; button 3(LOW ACTIVE)|
|CH423s-OC0|Q1|Output; Line 0 DC input analog switch|
|CH423s-OC1|Q2|Output; Line 0 AC input analog switch|
|CH423s-OC2|Q3|Output; Line 0 1차 1/10 감쇠|
|CH423s-OC3|Q4|Output; Line 0 1차 1/100 감쇠|
|CH423s-OC4|Q5|Output; Line 0 2차 1/2 감쇠|
|CH423s-OC5|Q6|Output; Line 0 2차 1/5 감쇠|
|CH423s-OC6|Q7|Output; Line 1 DC input analog switch|
|CH423s-OC7|Q8|Output; Line 1 AC input analog switch|
|CH423s-OC8|Q9|Output; Line 1 1차 1/10 감쇠|
|CH423s-OC9|Q0|Output; Line 1 1차 1/100 감쇠|
|CH423s-OC10|Q11|Output; Line 1 2차 1/2 감쇠|
|CH423s-OC11|Q12|Output; Line 1 2차 1/5 감쇠|
|CH423s-OC12|LED0|Output; GP LED (Red)|
|CH423s-OC13|LED1|Output; GP LED (Green)|
|CH423s-OC14|LEDRE0|Output; Rotary Encoder 0 LED (Green)|
|CH423s-OC15|LEDRE1|Output; Rotary Encoder 1 LED (Green)|

## 하드웨어 테스트 프로그램

### 테스트 기능

프로젝트에는 모든 하드웨어 기능을 테스트할 수 있는 종합 테스트 프로그램이 포함되어 있습니다.

#### 1. GPIO 출력 테스트
- 트리거 출력 핀 (GPIO25) 테스트
- HIGH/LOW 상태 토글 확인

#### 2. ADC 읽기 테스트
- ADC0 (GPIO36) 및 ADC1 (GPIO37) 채널 테스트
- 12비트 해상도, 11dB 감쇠 설정
- 전압 측정 및 mV 단위 변환

#### 3. I2C CH423 테스트
- CH423 IO 확장기 통신 테스트
- I2C 주소 0x40 사용
- 쓰기/읽기 기능 확인

#### 4. 로터리 인코더 테스트
- RE0 (GPIO13, GPIO12) 및 RE1 (GPIO14, GPIO27) 테스트
- A/B 채널 상태 읽기
- 풀업 저항 설정

#### 5. 트리거 시스템 테스트
- TRIG0 (GPIO17) 및 TRIG1 (GPIO16) 입력 테스트
- 오픈 드레인 입력 상태 확인

#### 6. 디스플레이 인터페이스 테스트
- SPI 통신 (FT800Q-T) 초기화 테스트
- SPI2_HOST 사용, 1MHz 클럭

### 테스트 실행 방법

1. **빌드**
   ```bash
   idf.py build
   ```

2. **플래시 및 모니터링**
   ```bash
   idf.py -p COM_PORT flash monitor
   ```

3. **테스트 결과 확인**
   - 시리얼 모니터에서 각 테스트 단계별 결과 확인
   - 모든 테스트 통과 시 "✅ All tests PASSED!" 메시지 출력

### 예상 출력

```
I (1234) OSCILLOSCOPE_TEST: Small Oscilloscope Hardware Test Starting...
I (1235) OSCILLOSCOPE_TEST: Test task created. Monitoring system...
I (2235) OSCILLOSCOPE_TEST: Starting comprehensive hardware test...
I (2236) OSCILLOSCOPE_TEST: === Testing GPIO Outputs ===
I (2237) OSCILLOSCOPE_TEST: Testing trigger output pin (GPIO25)
I (2337) OSCILLOSCOPE_TEST: GPIO output test completed
I (3337) OSCILLOSCOPE_TEST: === Testing ADC Readings ===
I (3338) OSCILLOSCOPE_TEST: ADC0: Raw=2048, Voltage=1650mV
I (3339) OSCILLOSCOPE_TEST: ADC1: Raw=2048, Voltage=1650mV
I (3340) OSCILLOSCOPE_TEST: ADC test completed
...
I (10340) OSCILLOSCOPE_TEST: === Test Results ===
I (10341) OSCILLOSCOPE_TEST: ✅ All tests PASSED!
I (10342) OSCILLOSCOPE_TEST: Hardware test completed. System ready for oscilloscope operation.
```

