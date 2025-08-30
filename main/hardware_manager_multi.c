#include "hardware_manager_multi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "MULTI_BUFFER";

// 다중 버퍼 초기화
esp_err_t multi_buffer_init(multi_buffer_manager_t *mbm)
{
    if (!mbm) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 뮤텍스 생성
    mbm->mutex = xSemaphoreCreateMutex();
    if (!mbm->mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // 이벤트 큐 생성
    mbm->event_queue = xQueueCreate(10, sizeof(buffer_event_data_t));
    if (!mbm->event_queue) {
        ESP_LOGE(TAG, "Failed to create event queue");
        vSemaphoreDelete(mbm->mutex);
        return ESP_ERR_NO_MEM;
    }
    
    // 버퍼 초기화
    for (int i = 0; i < MULTI_BUFFER_COUNT; i++) {
        memset(&mbm->buffers[i], 0, sizeof(multi_buffer_t));
        mbm->buffers[i].state = BUFFER_STATE_IDLE;
    }
    
    mbm->current_buffer = 0;
    mbm->ready_buffer = 0;
    mbm->capture_buffer = 0;
    mbm->initialized = true;
    
    // 첫 번째 버퍼를 채우기 상태로 설정
    mbm->buffers[0].state = BUFFER_STATE_FILLING;
    mbm->buffers[0].start_time = esp_timer_get_time();
    
    ESP_LOGI(TAG, "Multi-buffer manager initialized with %d buffers (32-bit original data)", MULTI_BUFFER_COUNT);
    return ESP_OK;
}

// 다중 버퍼 정리
esp_err_t multi_buffer_deinit(multi_buffer_manager_t *mbm)
{
    if (!mbm || !mbm->initialized) {
        return ESP_OK;
    }
    
    if (mbm->mutex) {
        vSemaphoreDelete(mbm->mutex);
        mbm->mutex = NULL;
    }
    
    if (mbm->event_queue) {
        vQueueDelete(mbm->event_queue);
        mbm->event_queue = NULL;
    }
    
    mbm->initialized = false;
    ESP_LOGI(TAG, "Multi-buffer manager deinitialized");
    return ESP_OK;
}

// 다중 버퍼에 32-bit 원본 데이터 쓰기
esp_err_t multi_buffer_write(multi_buffer_manager_t *mbm, uint32_t value, uint64_t timestamp)
{
    if (!mbm || !mbm->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(mbm->mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    multi_buffer_t *current = &mbm->buffers[mbm->current_buffer];
    
    // 현재 버퍼가 가득 찬 경우 다음 버퍼로 전환
    if (current->write_index >= MULTI_BUFFER_SIZE) {
        // 현재 버퍼를 가득 참 상태로 설정
        current->state = BUFFER_STATE_FULL;
        current->end_time = timestamp;
        
        // 이벤트 큐에 버퍼 가득 참 이벤트 전송
        buffer_event_data_t event = {
            .event_type = BUFFER_EVENT_FULL,
            .buffer_index = mbm->current_buffer,
            .trigger_index = 0,
            .trigger_time = 0,
            .trigger_level = 0
        };
        xQueueSend(mbm->event_queue, &event, 0);
        
        // 다음 버퍼로 전환
        mbm->current_buffer = (mbm->current_buffer + 1) % MULTI_BUFFER_COUNT;
        current = &mbm->buffers[mbm->current_buffer];
        
        // 새 버퍼 초기화
        current->write_index = 0;
        current->sample_count = 0;
        current->state = BUFFER_STATE_FILLING;
        current->start_time = timestamp;
        current->trigger_detected = false;
        
        ESP_LOGD(TAG, "Switched to buffer %lu", (unsigned long)mbm->current_buffer);
    }
    
    // 32-bit 원본 데이터 저장 (가공 없이)
    current->buffer[current->write_index] = value;
    current->timestamps[current->write_index] = timestamp;
    current->write_index++;
    current->sample_count++;
    
    xSemaphoreGive(mbm->mutex);
    return ESP_OK;
}

// 준비된 32-bit 원본 데이터 가져오기
esp_err_t multi_buffer_get_ready_data(multi_buffer_manager_t *mbm, uint32_t *data, uint64_t *timestamps, uint32_t *count)
{
    if (!mbm || !data || !timestamps || !count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(mbm->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // 준비된 버퍼 찾기
    bool found_ready = false;
    for (int i = 0; i < MULTI_BUFFER_COUNT; i++) {
        uint32_t idx = (mbm->ready_buffer + i) % MULTI_BUFFER_COUNT;
        if (mbm->buffers[idx].state == BUFFER_STATE_READY) {
            // 32-bit 원본 데이터 복사
            memcpy(data, mbm->buffers[idx].buffer, mbm->buffers[idx].sample_count * sizeof(uint32_t));
            memcpy(timestamps, mbm->buffers[idx].timestamps, mbm->buffers[idx].sample_count * sizeof(uint64_t));
            *count = mbm->buffers[idx].sample_count;
            
            // 버퍼를 대기 상태로 리셋
            mbm->buffers[idx].state = BUFFER_STATE_IDLE;
            mbm->ready_buffer = (idx + 1) % MULTI_BUFFER_COUNT;
            found_ready = true;
            break;
        }
    }
    
    xSemaphoreGive(mbm->mutex);
    
    if (!found_ready) {
        return ESP_ERR_NOT_FOUND;
    }
    
    return ESP_OK;
}

// 트리거 감지
esp_err_t multi_buffer_check_trigger(multi_buffer_manager_t *mbm, uint32_t trigger_level, hw_trig_edge_t edge)
{
    if (!mbm || !mbm->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(mbm->mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    multi_buffer_t *current = &mbm->buffers[mbm->current_buffer];
    
    // 최소 2개 샘플이 있어야 트리거 감지 가능
    if (current->sample_count < 2) {
        xSemaphoreGive(mbm->mutex);
        return ESP_ERR_NOT_FOUND;
    }
    
    // 최신 2개 샘플 확인 (32-bit 원본 데이터)
    uint32_t latest_idx = current->write_index - 1;
    uint32_t prev_idx = current->write_index - 2;
    uint32_t latest_value = current->buffer[latest_idx];
    uint32_t prev_value = current->buffer[prev_idx];
    
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
            xSemaphoreGive(mbm->mutex);
            return ESP_ERR_INVALID_ARG;
    }
    
    if (trigger_condition) {
        // 트리거 감지됨
        current->trigger_detected = true;
        current->trigger_index = latest_idx;
        current->trigger_time = current->timestamps[latest_idx];
        current->state = BUFFER_STATE_CAPTURING;
        
        // 트리거 이벤트 전송
        buffer_event_data_t event = {
            .event_type = BUFFER_EVENT_TRIGGER,
            .buffer_index = mbm->current_buffer,
            .trigger_index = latest_idx,
            .trigger_time = current->timestamps[latest_idx],
            .trigger_level = trigger_level
        };
        xQueueSend(mbm->event_queue, &event, 0);
        
        ESP_LOGI(TAG, "Trigger detected in buffer %lu at index %lu (32-bit value: %lu)", 
                 (unsigned long)mbm->current_buffer, (unsigned long)latest_idx, (unsigned long)latest_value);
        xSemaphoreGive(mbm->mutex);
        return ESP_OK;
    }
    
    xSemaphoreGive(mbm->mutex);
    return ESP_ERR_NOT_FOUND;
}

// 트리거 데이터 캡처 (32-bit 원본)
esp_err_t multi_buffer_capture_trigger_data(multi_buffer_manager_t *mbm, uint32_t *pre_data, uint32_t *post_data, 
                                           uint64_t *pre_timestamps, uint64_t *post_timestamps)
{
    if (!mbm || !pre_data || !post_data || !pre_timestamps || !post_timestamps) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(mbm->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // 캡처 중인 버퍼 찾기
    bool found_capturing = false;
    for (int i = 0; i < MULTI_BUFFER_COUNT; i++) {
        if (mbm->buffers[i].state == BUFFER_STATE_CAPTURING && mbm->buffers[i].trigger_detected) {
            multi_buffer_t *capture = &mbm->buffers[i];
            
            // 트리거 전 32-bit 원본 데이터 복사
            uint32_t pre_start = (capture->trigger_index >= MULTI_TRIG_PRE_SAMPLES) ? 
                                capture->trigger_index - MULTI_TRIG_PRE_SAMPLES : 0;
            uint32_t pre_count = capture->trigger_index - pre_start;
            
            for (uint32_t j = 0; j < pre_count; j++) {
                pre_data[j] = capture->buffer[pre_start + j];
                pre_timestamps[j] = capture->timestamps[pre_start + j];
            }
            
            // 트리거 후 32-bit 원본 데이터 복사
            uint32_t post_start = capture->trigger_index + 1;
            uint32_t post_count = (post_start + MULTI_TRIG_POST_SAMPLES <= capture->sample_count) ? 
                                 MULTI_TRIG_POST_SAMPLES : (capture->sample_count - post_start);
            
            for (uint32_t j = 0; j < post_count; j++) {
                post_data[j] = capture->buffer[post_start + j];
                post_timestamps[j] = capture->timestamps[post_start + j];
            }
            
            // 버퍼를 준비 상태로 설정
            capture->state = BUFFER_STATE_READY;
            found_capturing = true;
            break;
        }
    }
    
    xSemaphoreGive(mbm->mutex);
    
    if (!found_capturing) {
        return ESP_ERR_NOT_FOUND;
    }
    
    return ESP_OK;
}

// 이벤트 대기
esp_err_t multi_buffer_wait_event(multi_buffer_manager_t *mbm, buffer_event_data_t *event, uint32_t timeout_ms)
{
    if (!mbm || !event) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xQueueReceive(mbm->event_queue, event, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// 32-bit에서 16-bit 변환 (표시용)
esp_err_t multi_buffer_convert_to_16bit(uint32_t *src_32bit, uint16_t *dst_16bit, uint32_t count)
{
    if (!src_32bit || !dst_16bit) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 32-bit 원본 데이터를 16-bit로 변환 (표시용)
    for (uint32_t i = 0; i < count; i++) {
        // 32-bit 값을 16-bit로 변환 (상위 16비트 사용 또는 스케일링)
        uint32_t value_32bit = src_32bit[i];
        
        // 방법 1: 상위 16비트 사용
        dst_16bit[i] = (uint16_t)(value_32bit >> 16);
        
        // 방법 2: 스케일링 (필요시 주석 해제)
        // dst_16bit[i] = (uint16_t)((value_32bit * 65535) / 0xFFFFFFFF);
        
        // 방법 3: 12비트 ADC 값을 16비트로 확장 (필요시 주석 해제)
        // dst_16bit[i] = (uint16_t)((value_32bit & 0xFFF) << 4);
    }
    
    return ESP_OK;
}

// 16-bit 표시용 데이터 가져오기
esp_err_t multi_buffer_get_16bit_display_data(multi_buffer_manager_t *mbm, uint16_t *display_data, uint32_t *count)
{
    if (!mbm || !display_data || !count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 임시 32-bit 버퍼
    uint32_t temp_32bit[MULTI_BUFFER_SIZE];
    uint64_t temp_timestamps[MULTI_BUFFER_SIZE];
    uint32_t temp_count;
    
    // 32-bit 원본 데이터 가져오기
    esp_err_t ret = multi_buffer_get_ready_data(mbm, temp_32bit, temp_timestamps, &temp_count);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 32-bit를 16-bit로 변환
    ret = multi_buffer_convert_to_16bit(temp_32bit, display_data, temp_count);
    if (ret != ESP_OK) {
        return ret;
    }
    
    *count = temp_count;
    return ESP_OK;
}
