#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "can_logger.h"
#include "signal_decode.h"

static const char *TAG = "can_log";

// 目标 PSRAM 缓冲大小。8MB PSRAM 中留出余量给 WiFi/LWIP 等系统堆使用
#define CAN_LOG_PSRAM_SIZE  (6 * 1024 * 1024)

typedef struct {
    can_msg_entry_t *base;      // PSRAM 缓冲指针
    uint32_t         capacity;  // 总容量（条数）
    volatile uint32_t count;    // 已录制条数
    volatile bool    recording; // 是否正在录制
    volatile uint32_t dropped;  // 录满后丢弃的条数
    uint32_t         start_ms;  // 本次录制开始的时间戳
    uint32_t         stop_ms;   // 停止时的时间戳
    bool             psram_ok;  // PSRAM 分配是否成功
    SemaphoreHandle_t mutex;
} can_logger_t;

static can_logger_t s_log = {
    .base = NULL,
    .capacity = 0,
    .count = 0,
    .recording = false,
    .dropped = 0,
    .start_ms = 0,
    .stop_ms = 0,
    .psram_ok = false,
    .mutex = NULL,
};

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

esp_err_t can_log_init(void)
{
    s_log.mutex = xSemaphoreCreateMutex();
    if (!s_log.mutex) {
        return ESP_ERR_NO_MEM;
    }

    // 尝试分配 6MB PSRAM，失败则按剩余最大连续块降级
    s_log.base = heap_caps_malloc(CAN_LOG_PSRAM_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_log.base) {
        size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
        // 对齐到条数整数倍
        largest -= largest % sizeof(can_msg_entry_t);
        if (largest >= sizeof(can_msg_entry_t) * 1024) {
            s_log.base = heap_caps_malloc(largest, MALLOC_CAP_SPIRAM);
        }
    }
    if (!s_log.base) {
        ESP_LOGW(TAG, "PSRAM alloc failed, recording disabled");
        s_log.capacity = 0;
        s_log.psram_ok = false;
        return ESP_OK; // 不致命：监控功能不受影响
    }

    // 重新计算实际容量（可能是降级分配的大小）
    size_t got = heap_caps_get_allocated_size(s_log.base);
    s_log.capacity = got / sizeof(can_msg_entry_t);
    s_log.psram_ok = true;
    ESP_LOGI(TAG, "PSRAM log buffer: %u bytes, %u entries (entry=%u bytes)",
             (unsigned)(s_log.capacity * sizeof(can_msg_entry_t)),
             (unsigned)s_log.capacity, (unsigned)sizeof(can_msg_entry_t));
    return ESP_OK;
}

esp_err_t can_log_start(void)
{
    if (!s_log.psram_ok) {
        ESP_LOGE(TAG, "Start failed: PSRAM not allocated (SPIRAM not enabled or not detected)");
        return ESP_ERR_NOT_SUPPORTED;
    }

    xSemaphoreTake(s_log.mutex, portMAX_DELAY);
    s_log.count = 0;
    s_log.dropped = 0;
    s_log.start_ms = now_ms();
    s_log.stop_ms = 0;
    s_log.recording = true;
    xSemaphoreGive(s_log.mutex);
    ESP_LOGI(TAG, "Recording started (capacity=%u)", (unsigned)s_log.capacity);
    return ESP_OK;
}

esp_err_t can_log_stop(void)
{
    if (!s_log.psram_ok) return ESP_ERR_NOT_SUPPORTED;

    xSemaphoreTake(s_log.mutex, portMAX_DELAY);
    if (s_log.recording) {
        s_log.recording = false;
        s_log.stop_ms = now_ms();
    }
    xSemaphoreGive(s_log.mutex);
    ESP_LOGI(TAG, "Recording stopped (count=%u dropped=%u)",
             (unsigned)s_log.count, (unsigned)s_log.dropped);
    return ESP_OK;
}

esp_err_t can_log_clear(void)
{
    if (!s_log.psram_ok) return ESP_ERR_NOT_SUPPORTED;

    xSemaphoreTake(s_log.mutex, portMAX_DELAY);
    s_log.count = 0;
    s_log.dropped = 0;
    s_log.start_ms = 0;
    s_log.stop_ms = 0;
    s_log.recording = false;
    xSemaphoreGive(s_log.mutex);
    return ESP_OK;
}

bool can_log_is_recording(void)
{
    xSemaphoreTake(s_log.mutex, portMAX_DELAY);
    bool r = s_log.recording;
    xSemaphoreGive(s_log.mutex);
    return r;
}

void can_log_get_status(can_log_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->psram_ok = s_log.psram_ok;
    out->capacity = s_log.capacity;

    // 只短暂持锁拷贝易变字段，duration 在锁外计算
    xSemaphoreTake(s_log.mutex, portMAX_DELAY);
    out->recording = s_log.recording;
    out->count = s_log.count;
    out->dropped = s_log.dropped;
    uint32_t start = s_log.start_ms;
    uint32_t stop = s_log.stop_ms;
    xSemaphoreGive(s_log.mutex);

    if (s_log.psram_ok) {
        if (out->recording) {
            out->duration_ms = now_ms() - start;
        } else if (start > 0 && stop >= start) {
            out->duration_ms = stop - start;
        }
    }
}

esp_err_t can_log_get_buffer(can_msg_entry_t **base, uint32_t *count)
{
    if (!s_log.psram_ok) return ESP_ERR_NOT_SUPPORTED;
    *base = s_log.base;
    xSemaphoreTake(s_log.mutex, portMAX_DELAY);
    uint32_t n = s_log.count;
    xSemaphoreGive(s_log.mutex);
    *count = n;
    return ESP_OK;
}

void can_log_write(const can_msg_entry_t *entry)
{
    if (!s_log.recording) return;

    // 只录制目标报文 ID，其余直接忽略
    if (entry->id != SIG_ID_MOTOR_DRIVE && entry->id != SIG_ID_BUS_VI) return;

    // 快路径：未满则写入（单写者无锁；count 后置保证读侧快照一致性）
    uint32_t idx = s_log.count;
    if (idx < s_log.capacity) {
        s_log.base[idx] = *entry;
        s_log.count = idx + 1;
        return;
    }

    // 录满：走 mutex 明确终止（A4：停止状态机由锁保护，避免与读侧竞态）
    xSemaphoreTake(s_log.mutex, portMAX_DELAY);
    if (s_log.recording) {
        s_log.recording = false;
        s_log.stop_ms = now_ms();
        s_log.dropped++;
        ESP_LOGW(TAG, "Buffer full, recording stopped (count=%u)", (unsigned)s_log.count);
    }
    xSemaphoreGive(s_log.mutex);
}
