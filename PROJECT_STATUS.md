# Small Oscilloscope Project Status

## 완료된 작업 ✅

### 1. 하드웨어 설계
- [x] KiCad 스키매틱 설계 (hardware/small_oscilloscope/)
- [x] PCB 레이아웃 설계
- [x] 부품 라이브러리 구성
- [x] 핀 매핑 정의 (README.md)

### 2. 소프트웨어 개발
- [x] ESP-IDF 프로젝트 구조 설정
- [x] 종합 하드웨어 테스트 프로그램 작성
- [x] 모든 핀 정의 및 테스트 함수 구현
- [x] 빌드 설정 파일 생성

### 3. 테스트 프로그램 기능
- [x] GPIO 출력 테스트 (트리거 출력)
- [x] ADC 읽기 테스트 (2채널)
- [x] I2C CH423 통신 테스트
- [x] 로터리 인코더 테스트 (2개)
- [x] 트리거 시스템 테스트
- [x] SPI 디스플레이 인터페이스 테스트

### 4. 빌드 시스템
- [x] CMakeLists.txt 설정
- [x] sdkconfig.defaults 설정
- [x] Windows 빌드 스크립트 (build.bat, build.ps1)
- [x] 헤더 파일 구조화

### 5. 문서화
- [x] README.md 업데이트
- [x] 개발 환경 설정 가이드
- [x] 테스트 프로그램 사용법
- [x] 예상 출력 예시

## 하드웨어 사양

### 주요 구성 요소
- **MCU**: LOLIN D32 (ESP32)
- **디스플레이**: FT800Q-T (480x272)
- **IO 확장기**: CH423 (I2C)
- **ADC**: ESP32 내장 12비트 ADC
- **인터페이스**: I2C, SPI, GPIO

### 핀 할당
- **I2C**: SCL=22, SDA=21
- **SPI**: SS=5, MOSI=23, SCLK=18
- **ADC**: ADC0=36, ADC1=37
- **트리거**: TRIG0=17, TRIG1=16, TRIG_OUT=25
- **로터리 인코더**: RE0(A=13,B=12), RE1(A=14,B=27)

## 다음 단계

### 1. 하드웨어 제작
- [ ] PCB 제작 및 부품 조립
- [ ] 하드웨어 검증 테스트

### 2. 오실로스코프 기능 구현
- [ ] FT800Q-T 디스플레이 드라이버
- [ ] 신호 샘플링 및 처리
- [ ] 트리거 시스템 구현
- [ ] UI 인터페이스 개발

### 3. 고급 기능
- [ ] 측정 기능 (주파수, 진폭 등)
- [ ] 저장 및 재생 기능
- [ ] 배터리 관리 시스템

## 빌드 및 테스트 방법

1. **ESP-IDF 설치**
   ```bash
   # Windows Installer 사용 권장
   # https://dl.espressif.com/dl/esp-idf/
   ```

2. **프로젝트 빌드**
   ```bash
   idf.py build
   ```

3. **플래시 및 테스트**
   ```bash
   idf.py -p COM_PORT flash monitor
   ```

## 문제 해결

### 빌드 오류
- ESP-IDF 환경이 올바르게 설정되었는지 확인
- `idf.py --version` 명령으로 설치 확인

### 하드웨어 연결 문제
- 핀 매핑이 README.md와 일치하는지 확인
- 전원 공급 및 접지 연결 확인

## 프로젝트 정보
- **프로젝트명**: Small Oscilloscope
- **버전**: 1.0.0
- **상태**: 하드웨어 테스트 프로그램 완료
- **마지막 업데이트**: 2024년 현재 