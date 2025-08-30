#ifndef HARDWARE_MANAGER_MULTI_H
#define HARDWARE_MANAGER_MULTI_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "hardware_manager.h"

// 다중 버퍼 설정
#define MULTI_BUFFER_COUNT           4     // 4개의 버퍼
#define MULTI_BUFFER_SIZE            2048  // 각 버퍼 2K 샘플
#define MULTI_TRIG_PRE_SAMPLES       500   // 트리거 전 500 샘플
#define MULTI_TRIG_POST_SAMPLES      500   // 트리거 후 500 샘플

// 버퍼 상태
typedef enum {
    BUFFER_STATE_IDLE = 0,      // 대기 상태
    BUFFER_STATE_FILLING,       // 채우는 중
    BUFFER_STATE_FULL,          // 가득 참
    BUFFER_STATE_CAPTURING,     // 캡처 중
    BUFFER_STATE_READY          // 읽기 준비 완료
} buffer_state_t;

// 개별 버퍼 구조체 (32-bit 원본 데이터만 저장)
typedef struct {
    uint32_t buffer[MULTI_BUFFER_SIZE];    // 32-bit 원본 데이터
    uint64_t timestamps[MULTI_BUFFER_SIZE]; // 타임스탬프
    uint32_t write_index;
    uint32_t sample_count;
    buffer_state_t state;
    uint64_t start_time;
    uint64_t end_time;
    bool trigger_detected;
    uint32_t trigger_index;
    uint64_t trigger_time;
} multi_buffer_t;

// 다중 버퍼 관리자
typedef struct {
    multi_buffer_t buffers[MULTI_BUFFER_COUNT];
    uint32_t current_buffer;     // 현재 쓰기 중인 버퍼
    uint32_t ready_buffer;       // 읽기 준비된 버퍼
    uint32_t capture_buffer;     // 캡처 중인 버퍼
    SemaphoreHandle_t mutex;
    QueueHandle_t event_queue;
    bool initialized;
} multi_buffer_manager_t;

// 버퍼 이벤트
typedef enum {
    BUFFER_EVENT_FULL = 0,      // 버퍼 가득 참
    BUFFER_EVENT_TRIGGER,       // 트리거 감지
    BUFFER_EVENT_CAPTURE_READY  // 캡처 완료
} buffer_event_t;

// 버퍼 이벤트 구조체
typedef struct {
    buffer_event_t event_type;
    uint32_t buffer_index;
    uint32_t trigger_index;
    uint64_t trigger_time;
    uint32_t trigger_level;
} buffer_event_data_t;

// 다중 버퍼 관리 함수
esp_err_t multi_buffer_init(multi_buffer_manager_t *mbm);
esp_err_t multi_buffer_deinit(multi_buffer_manager_t *mbm);
esp_err_t multi_buffer_write(multi_buffer_manager_t *mbm, uint32_t value, uint64_t timestamp);
esp_err_t multi_buffer_get_ready_data(multi_buffer_manager_t *mbm, uint32_t *data, uint64_t *timestamps, uint32_t *count);

// 트리거 감지 함수
esp_err_t multi_buffer_check_trigger(multi_buffer_manager_t *mbm, uint32_t trigger_level, hw_trig_edge_t edge);
esp_err_t multi_buffer_capture_trigger_data(multi_buffer_manager_t *mbm, uint32_t *pre_data, uint32_t *post_data, 
                                           uint64_t *pre_timestamps, uint64_t *post_timestamps);

// 이벤트 처리 함수
esp_err_t multi_buffer_wait_event(multi_buffer_manager_t *mbm, buffer_event_data_t *event, uint32_t timeout_ms);

// 32-bit에서 16-bit 변환 함수 (표시용)
esp_err_t multi_buffer_convert_to_16bit(uint32_t *src_32bit, uint16_t *dst_16bit, uint32_t count);
esp_err_t multi_buffer_get_16bit_display_data(multi_buffer_manager_t *mbm, uint16_t *display_data, uint32_t *count);

#endif // HARDWARE_MANAGER_MULTI_H
