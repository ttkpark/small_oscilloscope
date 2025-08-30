#ifndef HARDWARE_MANAGER_CIRCULAR_H
#define HARDWARE_MANAGER_CIRCULAR_H

#include "hardware_manager.h"

// 순환 버퍼 설정
#define CIRCULAR_BUFFER_SIZE         8192  // 8K 샘플
#define TRIG_PRE_SAMPLES             1000  // 트리거 전 1000 샘플
#define TRIG_POST_SAMPLES            1000  // 트리거 후 1000 샘플
#define TRIG_TOTAL_SAMPLES           (TRIG_PRE_SAMPLES + TRIG_POST_SAMPLES)

// 순환 버퍼 구조체
typedef struct {
    uint32_t buffer[CIRCULAR_BUFFER_SIZE];
    uint32_t write_index;           // 현재 쓰기 위치
    uint32_t read_index;            // 현재 읽기 위치
    uint32_t sample_count;          // 총 샘플 수
    bool buffer_full;               // 버퍼 가득참 여부
    uint64_t timestamps[CIRCULAR_BUFFER_SIZE];  // 타임스탬프
} circular_buffer_t;

// 트리거 이벤트 구조체
typedef struct {
    uint32_t trigger_index;         // 트리거 발생 인덱스
    uint64_t trigger_time;          // 트리거 발생 시간
    uint32_t trigger_level;         // 트리거 레벨
    bool triggered;                 // 트리거 발생 여부
    uint32_t pre_samples[TRIG_PRE_SAMPLES];   // 트리거 전 샘플
    uint32_t post_samples[TRIG_POST_SAMPLES]; // 트리거 후 샘플
    uint64_t pre_timestamps[TRIG_PRE_SAMPLES];
    uint64_t post_timestamps[TRIG_POST_SAMPLES];
} trigger_event_t;

// 순환 버퍼 관리 함수
esp_err_t circular_buffer_init(circular_buffer_t *cb);
esp_err_t circular_buffer_write(circular_buffer_t *cb, uint32_t value, uint64_t timestamp);
esp_err_t circular_buffer_read_range(circular_buffer_t *cb, uint32_t start_idx, uint32_t count, 
                                    uint32_t *data, uint64_t *timestamps);
esp_err_t circular_buffer_get_latest_samples(circular_buffer_t *cb, uint32_t count, 
                                            uint32_t *data, uint64_t *timestamps);

// 트리거 기반 데이터 추출 함수
esp_err_t trigger_detect_and_capture(circular_buffer_t *cb, trigger_event_t *event, 
                                    uint32_t trigger_level, hw_trig_edge_t edge);
esp_err_t trigger_get_captured_data(trigger_event_t *event, uint32_t *data, uint64_t *timestamps);

#endif // HARDWARE_MANAGER_CIRCULAR_H

