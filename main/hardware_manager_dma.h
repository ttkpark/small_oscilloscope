#ifndef HARDWARE_MANAGER_DMA_H
#define HARDWARE_MANAGER_DMA_H

#include "hardware_manager.h"
#include "driver/dma.h"
#include "driver/gpio.h"

// DMA 버퍼 설정
#define DMA_BUFFER_COUNT             8     // 8개의 DMA 버퍼
#define DMA_BUFFER_SIZE              1024  // 각 버퍼 1K 샘플
#define DMA_TRIG_PRE_SAMPLES         512   // 트리거 전 512 샘플
#define DMA_TRIG_POST_SAMPLES        512   // 트리거 후 512 샘플
#define DMA_SAMPLE_RATE_HZ           100000 // 100kHz 샘플링

// DMA 채널 설정
#define DMA_CHANNEL_ADC              DMA_CHANNEL_0
#define DMA_CHANNEL_TRIG             DMA_CHANNEL_1

// DMA 버퍼 구조체
typedef struct {
    uint32_t buffer[DMA_BUFFER_SIZE];
    uint64_t timestamps[DMA_BUFFER_SIZE];
    uint32_t sample_count;
    bool buffer_full;
    bool trigger_detected;
    uint32_t trigger_index;
    uint64_t trigger_time;
    uint32_t trigger_level;
    dma_descriptor_t *descriptor;
} dma_buffer_t;

// DMA 관리자 구조체
typedef struct {
    dma_buffer_t buffers[DMA_BUFFER_COUNT];
    dma_descriptor_t *descriptors[DMA_BUFFER_COUNT];
    uint32_t current_buffer;
    uint32_t ready_buffer;
    uint32_t capture_buffer;
    dma_chan_handle_t dma_chan;
    SemaphoreHandle_t mutex;
    QueueHandle_t event_queue;
    bool initialized;
    bool running;
    uint64_t start_time;
    uint32_t total_samples;
} dma_manager_t;

// DMA 이벤트
typedef enum {
    DMA_EVENT_BUFFER_FULL = 0,    // DMA 버퍼 가득 참
    DMA_EVENT_TRIGGER_DETECTED,   // 트리거 감지
    DMA_EVENT_CAPTURE_COMPLETE,   // 캡처 완료
    DMA_EVENT_ERROR               // DMA 오류
} dma_event_t;

// DMA 이벤트 데이터
typedef struct {
    dma_event_t event_type;
    uint32_t buffer_index;
    uint32_t trigger_index;
    uint64_t trigger_time;
    uint32_t trigger_level;
    esp_err_t error_code;
} dma_event_data_t;

// DMA 관리 함수
esp_err_t dma_manager_init(dma_manager_t *dm);
esp_err_t dma_manager_deinit(dma_manager_t *dm);
esp_err_t dma_manager_start(dma_manager_t *dm);
esp_err_t dma_manager_stop(dma_manager_t *dm);

// DMA 데이터 접근 함수
esp_err_t dma_manager_get_ready_buffer(dma_manager_t *dm, uint32_t *data, uint64_t *timestamps, uint32_t *count);
esp_err_t dma_manager_get_trigger_data(dma_manager_t *dm, uint32_t *pre_data, uint32_t *post_data,
                                     uint64_t *pre_timestamps, uint64_t *post_timestamps);

// DMA 이벤트 처리 함수
esp_err_t dma_manager_wait_event(dma_manager_t *dm, dma_event_data_t *event, uint32_t timeout_ms);
esp_err_t dma_manager_check_trigger(dma_manager_t *dm, uint32_t trigger_level, hw_trig_edge_t edge);

// DMA 콜백 함수 (내부용)
void IRAM_ATTR dma_buffer_full_callback(dma_chan_handle_t chan, const dma_event_data_t *event, void *user_data);
void IRAM_ATTR dma_trigger_callback(dma_chan_handle_t chan, const dma_event_data_t *event, void *user_data);

#endif // HARDWARE_MANAGER_DMA_H

