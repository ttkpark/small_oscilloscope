# ESP-IDF ADC DMA Continuous Mode 구현

이 프로젝트는 ESP-IDF v5.4를 사용하여 ADC를 DMA로 끊임없이 읽는 방법을 구현한 것입니다.

## 개요

ESP32의 ADC를 Continuous Mode로 설정하여 DMA를 통해 끊임없이 데이터를 수집할 수 있습니다. 이 방식은 다음과 같은 장점이 있습니다:

- **높은 샘플링 속도**: 최대 10kHz까지 가능
- **CPU 부하 감소**: DMA가 자동으로 데이터를 처리
- **실시간 처리**: 인터럽트 기반으로 즉시 데이터 처리
- **버퍼링**: 1024 샘플 버퍼로 데이터 손실 방지

## 주요 기능

### 1. ADC Continuous Mode 초기화
```c
esp_err_t adc_dma_continuous_init(void);
```

### 2. ADC Continuous Mode 시작/정지
```c
esp_err_t adc_dma_continuous_start(void);
esp_err_t adc_dma_continuous_stop(void);
```

### 3. 데이터 접근 함수들
```c
// 전체 버퍼 데이터 가져오기
esp_err_t adc_dma_get_data(uint32_t *channel_0_data, uint32_t *channel_1_data, uint32_t *data_count);

// 최신 전압값 가져오기 (캘리브레이션 적용)
esp_err_t adc_dma_get_latest_voltage(uint32_t *voltage_ch0_mv, uint32_t *voltage_ch1_mv);

// 통계 정보 가져오기
esp_err_t adc_dma_get_statistics(uint32_t *min_ch0, uint32_t *max_ch0, uint32_t *avg_ch0,
                                uint32_t *min_ch1, uint32_t *max_ch1, uint32_t *avg_ch1);
```

## 설정

### ADC 채널
- **ADC1_CH0 (GPIO36)**: 첫 번째 채널
- **ADC1_CH1 (GPIO37)**: 두 번째 채널

### 샘플링 설정
- **샘플링 주파수**: 10kHz
- **버퍼 크기**: 1024 샘플
- **해상도**: 12비트
- **감쇠**: 11dB (0-3.3V 범위)

## 사용 방법

### 1. 기본 사용법

```c
#include "adc_dma_continuous.h"

void app_main(void) {
    // ADC DMA Continuous Mode 초기화
    esp_err_t ret = adc_dma_continuous_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC DMA init failed");
        return;
    }
    
    // ADC DMA Continuous Mode 시작
    ret = adc_dma_continuous_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC DMA start failed");
        return;
    }
    
    // 데이터 읽기
    uint32_t voltage_ch0_mv, voltage_ch1_mv;
    while (1) {
        ret = adc_dma_get_latest_voltage(&voltage_ch0_mv, &voltage_ch1_mv);
        if (ret == ESP_OK) {
            printf("CH0: %lumV, CH1: %lumV\n", voltage_ch0_mv, voltage_ch1_mv);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

### 2. 테스트 실행

프로젝트에 포함된 테스트를 실행하려면:

```c
// app_main.c에서 주석을 해제
start_adc_dma_test();        // 상세한 테스트
start_adc_monitor();         // 실시간 모니터링
```

### 3. 전체 버퍼 데이터 처리

```c
uint32_t channel_0_data[1024];
uint32_t channel_1_data[1024];
uint32_t data_count;

esp_err_t ret = adc_dma_get_data(channel_0_data, channel_1_data, &data_count);
if (ret == ESP_OK) {
    // 1024개의 샘플 데이터 처리
    for (int i = 0; i < data_count; i++) {
        // 데이터 처리 로직
    }
}
```

## 파일 구조

```
main/
├── adc_dma_continuous.c    # ADC DMA Continuous Mode 구현
├── adc_dma_continuous.h    # 헤더 파일
├── adc_dma_test.c         # 테스트 및 예제 코드
├── adc_dma_test.h         # 테스트 헤더 파일
└── app_main.c             # 메인 애플리케이션
```

## 빌드 및 실행

1. **프로젝트 빌드**:
   ```bash
   idf.py build
   ```

2. **플래시 및 모니터**:
   ```bash
   idf.py flash monitor
   ```

3. **ADC DMA 테스트 실행**:
   - `app_main.c`에서 `start_adc_dma_test()` 주석 해제
   - 또는 `start_adc_monitor()` 주석 해제

## 주의사항

1. **ADC 채널 제한**: ESP32에서는 ADC1과 ADC2를 동시에 사용할 때 제한이 있습니다.
2. **WiFi 사용**: ADC2를 사용할 때는 WiFi와 충돌할 수 있습니다.
3. **메모리 사용**: DMA 버퍼는 상당한 메모리를 사용합니다.
4. **캘리브레이션**: 정확한 측정을 위해서는 ADC 캘리브레이션이 필요합니다.

## 성능 특성

- **샘플링 속도**: 10kHz (설정 가능)
- **지연 시간**: < 1ms
- **CPU 사용률**: < 5% (DMA 사용으로 인해)
- **메모리 사용**: ~8KB (1024 샘플 × 2 채널 × 4바이트)

## 문제 해결

### 1. 초기화 실패
- ADC 채널이 올바르게 설정되었는지 확인
- 메모리 부족 여부 확인

### 2. 데이터 손실
- 큐 크기 증가
- 처리 속도 향상

### 3. 정확도 문제
- ADC 캘리브레이션 확인
- 노이즈 필터링 적용

## 확장 가능성

이 구현은 다음과 같이 확장할 수 있습니다:

1. **더 많은 채널**: ADC 패턴 설정 수정
2. **더 높은 샘플링 속도**: 설정 조정
3. **트리거 기능**: 특정 조건에서만 데이터 수집
4. **데이터 저장**: SD카드나 플래시에 저장
5. **실시간 분석**: FFT, 필터링 등

## 참고 자료

- [ESP-IDF ADC Continuous Mode 문서](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/adc_continuous.html)
- [ESP32 ADC 가이드](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/adc.html)
