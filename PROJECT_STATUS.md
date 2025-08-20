# Small Oscilloscope Project Status

## 프로젝트 개요
Small Oscilloscope는 ESP32 기반의 소형 오실로스코프 프로젝트입니다.

## 현재 상태 (2024-01-XX)

### ✅ 완료된 기능

#### 1. 하드웨어 설계
- [x] KiCad PCB 설계 완료
- [x] FT800 LCD 인터페이스 회로
- [x] CH423 I2C IO 확장 회로
- [x] 아날로그 입력 회로 (ADC)
- [x] 로터리 인코더 인터페이스
- [x] 트리거 회로
- [x] 전원 공급 회로

#### 2. 기본 소프트웨어
- [x] ESP-IDF 프로젝트 설정
- [x] FT800 LCD 드라이버 구현
- [x] 기본 GPIO 테스트
- [x] ADC 읽기 테스트
- [x] I2C CH423 테스트
- [x] 로터리 인코더 테스트

#### 3. 아날로그 테스트 시스템 ✅ **NEW**
- [x] CH423 테스트 회로 구현
- [x] 채널별 AC/DC 모드 선택 기능
- [x] 채널별 1차/2차 감쇠율 설정 (1X, 10X, 100X, 1000X)
- [x] 실시간 시리얼 출력 기능
- [x] 웹 서버를 통한 파형 표시
- [x] Python 테스트 스크립트
- [x] 종합 테스트 시스템

### 🔄 진행 중인 작업

#### 1. LCD 모니터 문제 해결
- [ ] FT800 LCD 동작 문제 디버깅
- [ ] SPI 통신 최적화
- [ ] 백라이트 제어 개선

#### 2. 아날로그 회로 테스트
- [ ] 실제 하드웨어에서 아날로그 테스트 실행
- [ ] ADC 캘리브레이션 개선
- [ ] 노이즈 필터링 적용

### 📋 예정된 작업

#### 1. 오실로스코프 핵심 기능
- [ ] 트리거 시스템 구현
- [ ] 시간축 제어 (Time/Div)
- [ ] 전압축 제어 (Volt/Div)
- [ ] 파형 저장 및 재생
- [ ] 측정 기능 (주파수, 진폭, 주기 등)

#### 2. 사용자 인터페이스
- [ ] LCD 터치 인터페이스 구현
- [ ] 메뉴 시스템
- [ ] 설정 저장/불러오기
- [ ] 다국어 지원

#### 3. 고급 기능
- [ ] FFT 스펙트럼 분석
- [ ] 자동 측정 기능
- [ ] 데이터 로깅
- [ ] 원격 제어 인터페이스

## 기술 스택

### 하드웨어
- **MCU**: ESP32-WROOM-32
- **LCD**: FT800 (SPI 인터페이스)
- **IO 확장**: CH423 (I2C)
- **ADC**: ESP32 내장 ADC (12-bit)
- **인코더**: ALPS EC11E

### 소프트웨어
- **프레임워크**: ESP-IDF v5.4
- **언어**: C/C++
- **통신**: SPI, I2C, UART
- **웹 서버**: ESP HTTP Server
- **테스트 도구**: Python + matplotlib

## 파일 구조

```
small_oscilloscope/
├── main/
│   ├── app_main.c              # 메인 애플리케이션
│   ├── ft800.c                 # FT800 LCD 드라이버
│   ├── ft800.h                 # FT800 헤더
│   ├── oscilloscope_test.c     # 기본 테스트 함수
│   ├── oscilloscope_test.h     # 기본 테스트 헤더
│   ├── analog_test.c           # 아날로그 테스트 시스템 ✅
│   ├── analog_test.h           # 아날로그 테스트 헤더 ✅
│   └── CMakeLists.txt          # 빌드 설정
├── hardware/
│   └── small_oscilloscope/     # KiCad 프로젝트
├── test_analog.py              # Python 테스트 스크립트 ✅
├── ANALOG_TEST_README.md       # 아날로그 테스트 문서 ✅
├── PROJECT_STATUS.md           # 이 파일
└── README.md                   # 프로젝트 개요
```

## 최근 업데이트 (2024-01-XX)

### 아날로그 테스트 시스템 구현 완료 ✅

요청하신 모든 아날로그 테스트 기능을 구현했습니다:

1. **CH423 테스트 회로**
   - I2C를 통한 CH423 IO 확장 칩 테스트
   - 테스트 패턴 출력 및 읽기 기능
   - 자동 감지 및 오류 처리

2. **채널별 AC/DC 모드 선택**
   - 각 채널 독립적으로 DC/AC 모드 설정
   - 실시간 모드 변경 지원
   - 설정 상태 시리얼 출력

3. **감쇠율 설정**
   - 1차/2차 감쇠율 독립 설정 (1X, 10X, 100X, 1000X)
   - 채널별 개별 설정 지원
   - 설정값 실시간 모니터링

4. **시리얼 출력**
   - 10Hz 실시간 데이터 출력
   - ADC 값, 채널 설정, CH423 상태 포함
   - 구조화된 데이터 형식

5. **웹 인터페이스**
   - 실시간 파형 표시 (Canvas 기반)
   - 채널 설정 웹 인터페이스
   - 실시간 데이터 모니터링
   - 반응형 디자인

6. **Python 테스트 도구**
   - 시리얼 데이터 수집 및 파싱
   - matplotlib을 통한 파형 플롯
   - 명령행 인터페이스
   - 자동화된 테스트 기능

### 주요 특징

- **실시간 성능**: 10Hz 업데이트율로 실시간 모니터링
- **멀티태스킹**: FreeRTOS 기반 병렬 처리
- **웹 기반**: 브라우저를 통한 원격 제어
- **확장 가능**: 모듈화된 설계로 기능 확장 용이
- **문서화**: 상세한 API 문서 및 사용법 제공

## 다음 단계

1. **하드웨어 테스트**: 실제 보드에서 아날로그 테스트 실행
2. **LCD 문제 해결**: FT800 동작 문제 디버깅
3. **성능 최적화**: ADC 샘플링율 및 정확도 개선
4. **오실로스코프 기능**: 트리거, 시간축, 측정 기능 구현

## 빌드 및 테스트

```bash
# 프로젝트 빌드
idf.py build

# ESP32 플래시 및 모니터링
idf.py flash monitor

# Python 테스트 스크립트 실행
python test_analog.py --port COM3 --plot
```

## 참고 자료

- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
- [FT800 Programming Guide](https://www.ftdichip.com/Support/Documents/ProgramGuides/FT800_Programmers_Guide.pdf)
- [CH423 Datasheet](https://www.wch.cn/downloads/CH423DS1_PDF.html)
- [ESP HTTP Server](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_server.html)

---

**마지막 업데이트**: 2024-01-XX
**상태**: 아날로그 테스트 시스템 구현 완료 ✅ 