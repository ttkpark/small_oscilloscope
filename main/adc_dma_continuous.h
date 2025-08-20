#ifndef ADC_DMA_CONTINUOUS_H
#define ADC_DMA_CONTINUOUS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ADC DMA Continuous Mode 초기화
esp_err_t adc_dma_continuous_init(void);

// ADC DMA Continuous Mode 시작
esp_err_t adc_dma_continuous_start(void);

// ADC DMA Continuous Mode 정지
esp_err_t adc_dma_continuous_stop(void);

// ADC 데이터 가져오기 (버퍼 전체)
esp_err_t adc_dma_get_data(uint32_t *channel_0_data, uint32_t *channel_1_data, uint32_t *data_count);

// ADC 최신 값 가져오기 (캘리브레이션 적용된 전압값)
esp_err_t adc_dma_get_latest_voltage(uint32_t *voltage_ch0_mv, uint32_t *voltage_ch1_mv);

// ADC 통계 정보 가져오기 (최소, 최대, 평균값)
esp_err_t adc_dma_get_statistics(uint32_t *min_ch0, uint32_t *max_ch0, uint32_t *avg_ch0,
                                uint32_t *min_ch1, uint32_t *max_ch1, uint32_t *avg_ch1);

// ADC DMA Continuous Mode 정리
void adc_dma_continuous_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // ADC_DMA_CONTINUOUS_H
