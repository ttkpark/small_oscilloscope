#ifndef ADC_DMA_CONTINUOUS_H
#define ADC_DMA_CONTINUOUS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// DMA 버퍼 설정 (메모리 사용량 줄임)
#define ADC_BUFFER_SIZE             256   // 256로 줄임
#define ADC_POLLING_BUFFER_SIZE     4
#define ADC_SAMPLE_FREQ_HZ          8000  // 100kHz

// Polling 주기 설정 (초 단위)
#define ADC_POLLING_INTERVAL_SEC    1  // 1초마다 Polling 수행

#ifdef __cplusplus
extern "C" {
#endif

// ADC Continuous + Polling Mode 초기화
esp_err_t adc_dma_continuous_init(void);

// ADC Continuous + Polling Mode 시작
esp_err_t adc_dma_continuous_start(void);

// ADC Continuous + Polling Mode 정지
esp_err_t adc_dma_continuous_stop(void);

// ADC 데이터 가져오기 (Continuous 모드 - GPIO34, GPIO35)
esp_err_t adc_dma_get_data(uint32_t *channel_0_data, uint32_t *channel_1_data, uint32_t *data_count);

// ADC 최신 값 가져오기 (캘리브레이션 적용된 전압값 - GPIO34, GPIO35)
esp_err_t adc_dma_get_latest_voltage(uint32_t *voltage_ch0_mv, uint32_t *voltage_ch1_mv);

// ADC 통계 정보 가져오기 (최소, 최대, 평균값 - GPIO34, GPIO35)
esp_err_t adc_dma_get_statistics(uint32_t *min_ch0, uint32_t *max_ch0, uint32_t *avg_ch0,
                                uint32_t *min_ch1, uint32_t *max_ch1, uint32_t *avg_ch1);

// ADC Polling 데이터 가져오기 (Polling 모드 - GPIO36-39)
esp_err_t adc_get_polling_data(uint32_t *ch0_data, uint32_t *ch1_data, uint32_t *ch2_data, uint32_t *ch3_data, uint32_t *data_count);

// ADC Continuous + Polling Mode 정리
void adc_dma_continuous_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // ADC_DMA_CONTINUOUS_H
