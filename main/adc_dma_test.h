#ifndef ADC_DMA_TEST_H
#define ADC_DMA_TEST_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ADC DMA Continuous Mode 테스트 시작
esp_err_t start_adc_dma_test(void);

// 실시간 ADC 모니터링 시작
esp_err_t start_adc_monitor(void);

#ifdef __cplusplus
}
#endif

#endif // ADC_DMA_TEST_H
