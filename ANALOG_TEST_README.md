# Small Oscilloscope - 아날로그 테스트 기능

이 문서는 Small Oscilloscope 프로젝트의 아날로그 회로 테스트 기능에 대한 설명입니다.

## 기능 개요

아날로그 테스트 시스템은 다음 기능들을 제공합니다:

1. **CH423 테스트 회로**: I2C를 통한 CH423 IO 확장 칩 테스트
2. **아날로그 신호 출력**: 채널별 AC/DC 모드 선택
3. **감쇠율 설정**: 채널별 1차/2차 감쇠율 설정 (1X, 10X, 100X, 1000X)
4. **실시간 시리얼 출력**: ADC 읽기 값과 설정 정보를 시리얼로 출력
5. **웹 인터페이스**: 브라우저를 통한 실시간 파형 표시 및 제어

## 하드웨어 요구사항

- ESP32 개발 보드
- CH423 IO 확장 칩 (I2C 주소: 0x40)
- ADC 입력 채널 2개 (GPIO36, GPIO37)
- I2C 연결 (SDA: GPIO21, SCL: GPIO22)

## 빌드 및 플래시

```bash
# 프로젝트 빌드
idf.py build

# ESP32에 플래시
idf.py flash monitor
```

## 사용법

### 1. 기본 실행

ESP32를 플래시하고 실행하면 자동으로 다음이 시작됩니다:

- 아날로그 테스트 시스템 초기화
- 웹 서버 시작 (기본 포트: 80)
- 시리얼 출력 태스크 시작 (10Hz)
- CH423 테스트 실행

### 2. 시리얼 모니터링

시리얼 포트를 통해 실시간 데이터를 확인할 수 있습니다:

```
ADC: CH0=1234mV, CH1=5678mV | CH0: DC, 1X/10X, 1500mV | CH1: AC, 10X/100X, 2000mV
```

### 3. 웹 인터페이스

브라우저에서 `http://[ESP32_IP]`로 접속하여 다음 기능을 사용할 수 있습니다:

- **연결 상태 확인**: 실시간 연결 상태 표시
- **채널 설정**: 각 채널의 AC/DC 모드, 감쇠율, 출력 전압 설정
- **파형 표시**: 실시간 ADC 값 그래프 표시
- **데이터 모니터링**: 현재 ADC 값과 설정 정보 표시

### 4. Python 테스트 스크립트

`test_analog.py` 스크립트를 사용하여 고급 테스트 기능을 사용할 수 있습니다:

```bash
# 기본 모니터링 (60초)
python test_analog.py --port COM3

# 데이터 플롯과 함께 모니터링
python test_analog.py --port COM3 --plot --duration 120

# CH423 테스트
python test_analog.py --port COM3 --test-ch423

# 채널 설정
python test_analog.py --port COM3 --set-ch0 DC 1 10 1500
python test_analog.py --port COM3 --set-ch1 AC 10 100 2000
```

## API 함수

### 초기화 및 설정

```c
// 아날로그 테스트 시스템 초기화
esp_err_t analog_test_init(void);

// 채널 설정
esp_err_t set_channel_config(uint8_t channel, const channel_config_t *config);

// 아날로그 출력 설정
esp_err_t set_analog_output(uint8_t channel, uint16_t voltage_mv);

// AC/DC 모드 설정
esp_err_t set_ac_dc_mode(uint8_t channel, ac_dc_mode_t mode);

// 감쇠율 설정
esp_err_t set_attenuation(uint8_t channel, attenuation_t primary, attenuation_t secondary);
```

### 테스트 함수

```c
// CH423 테스트
esp_err_t test_ch423_circuit(uint8_t *read_data);

// 종합 테스트 실행
esp_err_t run_analog_comprehensive_test(analog_test_results_t *results);

// 테스트 결과 출력
void print_analog_test_results(const analog_test_results_t *results);
```

### 서비스 함수

```c
// 시리얼 출력 태스크 시작
esp_err_t start_serial_output_task(void);

// 웹 서버 시작
esp_err_t start_web_server(void);
```

## 데이터 구조체

### 채널 설정

```c
typedef struct {
    ac_dc_mode_t ac_dc_mode;        // AC/DC 모드
    attenuation_t primary_atten;    // 1차 감쇠율
    attenuation_t secondary_atten;  // 2차 감쇠율
    uint16_t output_voltage_mv;     // 출력 전압 (mV)
    bool enabled;                   // 활성화 여부
} channel_config_t;
```

### 테스트 결과

```c
typedef struct {
    bool ch423_test_passed;         // CH423 테스트 통과 여부
    bool analog_output_test_passed; // 아날로그 출력 테스트 통과 여부
    bool serial_output_test_passed; // 시리얼 출력 테스트 통과 여부
    uint8_t ch423_read_data;        // CH423 읽기 데이터
    uint32_t adc_readings[2];       // ADC 읽기 값 (mV)
    channel_config_t channel_configs[2]; // 채널 설정
} analog_test_results_t;
```

## 설정 옵션

### AC/DC 모드
- `MODE_DC`: DC 모드 (직류)
- `MODE_AC`: AC 모드 (교류)

### 감쇠율
- `ATTEN_1X`: 1배 (감쇠 없음)
- `ATTEN_10X`: 10배 감쇠
- `ATTEN_100X`: 100배 감쇠
- `ATTEN_1000X`: 1000배 감쇠

## 웹 인터페이스 사용법

1. **연결**: ESP32의 IP 주소로 브라우저 접속
2. **채널 설정**: 각 채널의 설정을 변경하고 "Apply" 버튼 클릭
3. **파형 확인**: 실시간 파형이 캔버스에 표시됨
4. **데이터 모니터링**: 현재 ADC 값과 설정 정보 확인

## 문제 해결

### CH423 테스트 실패
- I2C 연결 확인 (SDA: GPIO21, SCL: GPIO22)
- 전원 공급 확인
- I2C 주소 확인 (0x40)

### 웹 서버 접속 불가
- ESP32 IP 주소 확인
- 네트워크 연결 상태 확인
- 방화벽 설정 확인

### ADC 읽기 오류
- ADC 핀 연결 확인 (GPIO36, GPIO37)
- 입력 전압 범위 확인 (0-3.3V)
- ADC 캘리브레이션 확인

## 로그 메시지

주요 로그 태그:
- `ANALOG_TEST`: 아날로그 테스트 관련 로그
- `SMALL_OSCILLOSCOPE`: 메인 애플리케이션 로그

로그 레벨 설정:
```bash
# 로그 레벨 설정
idf.py menuconfig
# Component config -> Log output -> Default log verbosity
```

## 성능 특성

- **ADC 샘플링**: 10Hz (100ms 간격)
- **웹 업데이트**: 실시간 (10Hz)
- **시리얼 출력**: 10Hz
- **메모리 사용량**: 약 8KB (태스크 스택)

## 향후 개선 사항

1. **WebSocket 지원**: 실시간 양방향 통신
2. **데이터 저장**: SD 카드 또는 플래시 메모리에 데이터 저장
3. **고급 필터링**: 디지털 필터 적용
4. **트리거 기능**: 자동 트리거 및 동기화
5. **캘리브레이션**: 자동 ADC 캘리브레이션

## 라이선스

이 프로젝트는 MIT 라이선스 하에 배포됩니다. 