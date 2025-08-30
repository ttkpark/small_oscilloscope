#include "hardware_manager_dma.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_adc/adc_continuous.h"
#include <string.h>

static const char *TAG = "DMA_MANAGER";

// 전역 DMA 관리자 인스턴스
static dma_manager_t *g_dma_manager = NULL;

// DMA 버퍼 가득 참 콜백 (IRAM에 위치)
void IRAM_ATTR dma_buffer_full_callback(dma_chan_handle_t chan, const dma_event_data_t *event, void *user_data)
{
    dma_manager_t *dm = (dma_manager_t *)user_data;
    BaseType_t must_yield = pdFALSE;
    
    if (dm && dm->event_queue) {
        dma_event_data_t dma_event = {
            .event_type = DMA_EVENT_BUFFER_FULL,
            .buffer_index = dm->current_buffer,
            .trigger_index = 0,
            .trigger_time = 0,
            .trigger_level = 0,
            .error_code = ESP_OK
        };
        
        xQueueSendFromISR(dm->event_queue, &dma_event, &must_yield);
    }
    
    portYIELD_FROM_ISR(must_yield);
}

// DMA 트리거 콜백 (IRAM에 위치)
void IRAM_ATTR dma_trigger_callback(dma_chan_handle_t chan, const dma_event_data_t *event, void *user_data)
{
    dma_manager_t *dm = (dma_manager_t *)user_data;
    BaseType_t must_yield = pdFALSE;
    
    if (dm && dm->event_queue) {
        dma_event_data_t dma_event = {
            .event_type = DMA_EVENT_TRIGGER_DETECTED,
            .buffer_index = dm->current_buffer,
            .trigger_index = dm->buffers[dm->current_buffer].trigger_index,
            .trigger_time = dm->buffers[dm->current_buffer].trigger_time,
            .trigger_level = dm->buffers[dm->current_buffer].trigger_level,
            .error_code = ESP_OK
        };
        
        xQueueSendFromISR(dm->event_queue, &dma_event, &must_yield);
    }
    
    portYIELD_FROM_ISR(must_yield);
}

// DMA 관리자 초기화
esp_err_t dma_manager_init(dma_manager_t *dm)
{
    if (!dm) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ESP_OK;
    
    // 뮤텍스 생성
    dm->mutex = xSemaphoreCreateMutex();
    if (!dm->mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // 이벤트 큐 생성
    dm->event_queue = xQueueCreate(20, sizeof(dma_event_data_t));
    if (!dm->event_queue) {
        ESP_LOGE(TAG, "Failed to create event queue");
        vSemaphoreDelete(dm->mutex);
        return ESP_ERR_NO_MEM;
    }
    
    // DMA 채널 할당
    dma_chan_alloc_cfg_t chan_cfg = {
        .direction = DMA_CHANNEL_DIRECTION_FROM_PERIPH,
        .flags = {
            .mb_desc = 1,
            .auto_retain = 1,
        },
    };
    ret = dma_new_channel(&chan_cfg, &dm->dma_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to allocate DMA channel: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // DMA 버퍼 초기화
    for (int i = 0; i < DMA_BUFFER_COUNT; i++) {
        memset(&dm->buffers[i], 0, sizeof(dma_buffer_t));
        
        // DMA 디스크립터 할당
        dma_desc_alloc_cfg_t desc_cfg = {
            .buffer_size = DMA_BUFFER_SIZE * sizeof(uint32_t),
            .flags = {
                .auto_retain = 1,
            },
        };
        ret = dma_alloc_desc(&desc_cfg, &dm->descriptors[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to allocate DMA descriptor %d: %s", i, esp_err_to_name(ret));
            goto cleanup;
        }
        
        dm->buffers[i].descriptor = dm->descriptors[i];
    }
    
    dm->current_buffer = 0;
    dm->ready_buffer = 0;
    dm->capture_buffer = 0;
    dm->initialized = true;
    dm->running = false;
    dm->total_samples = 0;
    g_dma_manager = dm;
    
    ESP_LOGI(TAG, "DMA manager initialized with %d buffers", DMA_BUFFER_COUNT);
    return ESP_OK;
    
cleanup:
    if (dm->dma_chan) {
        dma_del_channel(dm->dma_chan);
        dm->dma_chan = NULL;
    }
    
    for (int i = 0; i < DMA_BUFFER_COUNT; i++) {
        if (dm->descriptors[i]) {
            dma_free_desc(dm->descriptors[i]);
            dm->descriptors[i] = NULL;
        }
    }
    
    if (dm->mutex) {
        vSemaphoreDelete(dm->mutex);
        dm->mutex = NULL;
    }
    
    if (dm->event_queue) {
        vQueueDelete(dm->event_queue);
        dm->event_queue = NULL;
    }
    
    return ret;
}

// DMA 관리자 정리
esp_err_t dma_manager_deinit(dma_manager_t *dm)
{
    if (!dm || !dm->initialized) {
        return ESP_OK;
    }
    
    if (dm->running) {
        dma_manager_stop(dm);
    }
    
    if (dm->dma_chan) {
        dma_del_channel(dm->dma_chan);
        dm->dma_chan = NULL;
    }
    
    for (int i = 0; i < DMA_BUFFER_COUNT; i++) {
        if (dm->descriptors[i]) {
            dma_free_desc(dm->descriptors[i]);
            dm->descriptors[i] = NULL;
        }
    }
    
    if (dm->mutex) {
        vSemaphoreDelete(dm->mutex);
        dm->mutex = NULL;
    }
    
    if (dm->event_queue) {
        vQueueDelete(dm->event_queue);
        dm->event_queue = NULL;
    }
    
    dm->initialized = false;
    g_dma_manager = NULL;
    
    ESP_LOGI(TAG, "DMA manager deinitialized");
    return ESP_OK;
}

// DMA 관리자 시작
esp_err_t dma_manager_start(dma_manager_t *dm)
{
    if (!dm || !dm->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (dm->running) {
        ESP_LOGW(TAG, "DMA manager already running");
        return ESP_OK;
    }
    
    if (xSemaphoreTake(dm->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // DMA 콜백 등록
    dma_event_callbacks_t cbs = {
        .on_recv_eof = dma_buffer_full_callback,
    };
    esp_err_t ret = dma_register_event_callbacks(dm->dma_chan, &cbs, dm);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register DMA callbacks: %s", esp_err_to_name(ret));
        xSemaphoreGive(dm->mutex);
        return ret;
    }
    
    // DMA 시작
    ret = dma_start(dm->dma_chan, dm->descriptors[0]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start DMA: %s", esp_err_to_name(ret));
        xSemaphoreGive(dm->mutex);
        return ret;
    }
    
    dm->running = true;
    dm->start_time = esp_timer_get_time();
    dm->total_samples = 0;
    
    xSemaphoreGive(dm->mutex);
    
    ESP_LOGI(TAG, "DMA manager started");
    return ESP_OK;
}

// DMA 관리자 정지
esp_err_t dma_manager_stop(dma_manager_t *dm)
{
    if (!dm || !dm->running) {
        return ESP_OK;
    }
    
    if (xSemaphoreTake(dm->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // DMA 정지
    esp_err_t ret = dma_stop(dm->dma_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop DMA: %s", esp_err_to_name(ret));
    }
    
    dm->running = false;
    
    xSemaphoreGive(dm->mutex);
    
    ESP_LOGI(TAG, "DMA manager stopped");
    return ESP_OK;
}

// 준비된 DMA 버퍼 가져오기
esp_err_t dma_manager_get_ready_buffer(dma_manager_t *dm, uint32_t *data, uint64_t *timestamps, uint32_t *count)
{
    if (!dm || !data || !timestamps || !count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(dm->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // 준비된 버퍼 찾기
    bool found_ready = false;
    for (int i = 0; i < DMA_BUFFER_COUNT; i++) {
        uint32_t idx = (dm->ready_buffer + i) % DMA_BUFFER_COUNT;
        if (dm->buffers[idx].buffer_full) {
            // 데이터 복사
            memcpy(data, dm->buffers[idx].buffer, dm->buffers[idx].sample_count * sizeof(uint32_t));
            memcpy(timestamps, dm->buffers[idx].timestamps, dm->buffers[idx].sample_count * sizeof(uint64_t));
            *count = dm->buffers[idx].sample_count;
            
            // 버퍼 리셋
            dm->buffers[idx].buffer_full = false;
            dm->buffers[idx].sample_count = 0;
            dm->ready_buffer = (idx + 1) % DMA_BUFFER_COUNT;
            found_ready = true;
            break;
        }
    }
    
    xSemaphoreGive(dm->mutex);
    
    if (!found_ready) {
        return ESP_ERR_NOT_FOUND;
    }
    
    return ESP_OK;
}

// 트리거 데이터 가져오기
esp_err_t dma_manager_get_trigger_data(dma_manager_t *dm, uint32_t *pre_data, uint32_t *post_data,
                                     uint64_t *pre_timestamps, uint64_t *post_timestamps)
{
    if (!dm || !pre_data || !post_data || !pre_timestamps || !post_timestamps) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(dm->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // 트리거가 감지된 버퍼 찾기
    bool found_trigger = false;
    for (int i = 0; i < DMA_BUFFER_COUNT; i++) {
        if (dm->buffers[i].trigger_detected) {
            dma_buffer_t *trigger_buffer = &dm->buffers[i];
            
            // 트리거 전 데이터 복사
            uint32_t pre_start = (trigger_buffer->trigger_index >= DMA_TRIG_PRE_SAMPLES) ? 
                                trigger_buffer->trigger_index - DMA_TRIG_PRE_SAMPLES : 0;
            uint32_t pre_count = trigger_buffer->trigger_index - pre_start;
            
            for (uint32_t j = 0; j < pre_count; j++) {
                pre_data[j] = trigger_buffer->buffer[pre_start + j];
                pre_timestamps[j] = trigger_buffer->timestamps[pre_start + j];
            }
            
            // 트리거 후 데이터 복사
            uint32_t post_start = trigger_buffer->trigger_index + 1;
            uint32_t post_count = (post_start + DMA_TRIG_POST_SAMPLES <= trigger_buffer->sample_count) ? 
                                 DMA_TRIG_POST_SAMPLES : (trigger_buffer->sample_count - post_start);
            
            for (uint32_t j = 0; j < post_count; j++) {
                post_data[j] = trigger_buffer->buffer[post_start + j];
                post_timestamps[j] = trigger_buffer->timestamps[post_start + j];
            }
            
            // 트리거 플래그 리셋
            trigger_buffer->trigger_detected = false;
            found_trigger = true;
            break;
        }
    }
    
    xSemaphoreGive(dm->mutex);
    
    if (!found_trigger) {
        return ESP_ERR_NOT_FOUND;
    }
    
    return ESP_OK;
}

// DMA 이벤트 대기
esp_err_t dma_manager_wait_event(dma_manager_t *dm, dma_event_data_t *event, uint32_t timeout_ms)
{
    if (!dm || !event) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xQueueReceive(dm->event_queue, event, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// 트리거 감지 (실제로는 DMA 콜백에서 처리됨)
esp_err_t dma_manager_check_trigger(dma_manager_t *dm, uint32_t trigger_level, hw_trig_edge_t edge)
{
    if (!dm || !dm->running) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 이 함수는 주로 트리거 레벨과 엣지를 설정하는 용도
    // 실제 트리거 감지는 DMA 콜백에서 처리됨
    
    if (xSemaphoreTake(dm->mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // 현재 버퍼에 트리거 설정 저장
    dm->buffers[dm->current_buffer].trigger_level = trigger_level;
    
    xSemaphoreGive(dm->mutex);
    
    return ESP_OK;
}

