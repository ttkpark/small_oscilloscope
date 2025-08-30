#include "hardware_manager_circular.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "CIRCULAR_BUFFER";

// 순환 버퍼 초기화
esp_err_t circular_buffer_init(circular_buffer_t *cb)
{
    if (!cb) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(cb, 0, sizeof(circular_buffer_t));
    ESP_LOGI(TAG, "Circular buffer initialized with size %d", CIRCULAR_BUFFER_SIZE);
    return ESP_OK;
}

// 순환 버퍼에 데이터 쓰기
esp_err_t circular_buffer_write(circular_buffer_t *cb, uint32_t value, uint64_t timestamp)
{
    if (!cb) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 현재 위치에 데이터 저장
    cb->buffer[cb->write_index] = value;
    cb->timestamps[cb->write_index] = timestamp;
    
    // 인덱스 업데이트
    cb->write_index = (cb->write_index + 1) % CIRCULAR_BUFFER_SIZE;
    cb->sample_count++;
    
    // 버퍼가 가득 찬 경우
    if (cb->sample_count >= CIRCULAR_BUFFER_SIZE) {
        cb->buffer_full = true;
        cb->read_index = cb->write_index;  // 읽기 인덱스도 업데이트
    }
    
    return ESP_OK;
}

// 특정 범위의 데이터 읽기
esp_err_t circular_buffer_read_range(circular_buffer_t *cb, uint32_t start_idx, uint32_t count, 
                                    uint32_t *data, uint64_t *timestamps)
{
    if (!cb || !data || !timestamps) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (count > CIRCULAR_BUFFER_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx = (start_idx + i) % CIRCULAR_BUFFER_SIZE;
        data[i] = cb->buffer[idx];
        timestamps[i] = cb->timestamps[idx];
    }
    
    return ESP_OK;
}

// 최신 N개 샘플 읽기
esp_err_t circular_buffer_get_latest_samples(circular_buffer_t *cb, uint32_t count, 
                                            uint32_t *data, uint64_t *timestamps)
{
    if (!cb || !data || !timestamps) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (count > CIRCULAR_BUFFER_SIZE) {
        count = CIRCULAR_BUFFER_SIZE;
    }
    
    // 최신 데이터부터 역순으로 읽기
    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx = (cb->write_index - 1 - i + CIRCULAR_BUFFER_SIZE) % CIRCULAR_BUFFER_SIZE;
        data[count - 1 - i] = cb->buffer[idx];
        timestamps[count - 1 - i] = cb->timestamps[idx];
    }
    
    return ESP_OK;
}

// 트리거 감지 및 데이터 캡처
esp_err_t trigger_detect_and_capture(circular_buffer_t *cb, trigger_event_t *event, 
                                    uint32_t trigger_level, hw_trig_edge_t edge)
{
    if (!cb || !event) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 최신 샘플 확인
    uint32_t latest_idx = (cb->write_index - 1 + CIRCULAR_BUFFER_SIZE) % CIRCULAR_BUFFER_SIZE;
    uint32_t latest_value = cb->buffer[latest_idx];
    uint32_t prev_idx = (latest_idx - 1 + CIRCULAR_BUFFER_SIZE) % CIRCULAR_BUFFER_SIZE;
    uint32_t prev_value = cb->buffer[prev_idx];
    
    bool trigger_condition = false;
    
    // 트리거 조건 확인
    switch (edge) {
        case HW_TRIG_EDGE_RISING:
            trigger_condition = (prev_value < trigger_level) && (latest_value >= trigger_level);
            break;
        case HW_TRIG_EDGE_FALLING:
            trigger_condition = (prev_value > trigger_level) && (latest_value <= trigger_level);
            break;
        case HW_TRIG_EDGE_BOTH:
            trigger_condition = ((prev_value < trigger_level) && (latest_value >= trigger_level)) ||
                               ((prev_value > trigger_level) && (latest_value <= trigger_level));
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    
    if (trigger_condition) {
        // 트리거 이벤트 설정
        event->trigger_index = latest_idx;
        event->trigger_time = cb->timestamps[latest_idx];
        event->trigger_level = trigger_level;
        event->triggered = true;
        
        // 트리거 전 샘플 캡처
        for (uint32_t i = 0; i < TRIG_PRE_SAMPLES; i++) {
            uint32_t idx = (latest_idx - TRIG_PRE_SAMPLES + i + CIRCULAR_BUFFER_SIZE) % CIRCULAR_BUFFER_SIZE;
            event->pre_samples[i] = cb->buffer[idx];
            event->pre_timestamps[i] = cb->timestamps[idx];
        }
        
        // 트리거 후 샘플은 나중에 채워짐 (현재는 0으로 초기화)
        memset(event->post_samples, 0, sizeof(event->post_samples));
        memset(event->post_timestamps, 0, sizeof(event->post_timestamps));
        
        ESP_LOGI(TAG, "Trigger detected at index %lu, level %lu", latest_idx, trigger_level);
        return ESP_OK;
    }
    
    return ESP_ERR_NOT_FOUND;
}

// 캡처된 트리거 데이터 가져오기
esp_err_t trigger_get_captured_data(trigger_event_t *event, uint32_t *data, uint64_t *timestamps)
{
    if (!event || !data || !timestamps) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!event->triggered) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 트리거 전 데이터 복사
    memcpy(data, event->pre_samples, TRIG_PRE_SAMPLES * sizeof(uint32_t));
    memcpy(timestamps, event->pre_timestamps, TRIG_PRE_SAMPLES * sizeof(uint64_t));
    
    // 트리거 후 데이터 복사
    memcpy(data + TRIG_PRE_SAMPLES, event->post_samples, TRIG_POST_SAMPLES * sizeof(uint32_t));
    memcpy(timestamps + TRIG_PRE_SAMPLES, event->post_timestamps, TRIG_POST_SAMPLES * sizeof(uint64_t));
    
    return ESP_OK;
}

