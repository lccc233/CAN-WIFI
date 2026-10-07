#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "can_logger.h"
#include "signal_decode.h"

static const char *TAG = "can_log";

// 始终保留系统堆余量；降级时也不吞掉最大连续空闲块。
#define CAN_LOG_PSRAM_SIZE    (6 * 1024 * 1024)
#define CAN_LOG_PSRAM_RESERVE (512 * 1024)
#define CAN_LOG_ALLOC_MARGIN  (4 * 1024)
#define CAN_LOG_SYNC_CLIENTS  8
#define CAN_LOG_SYNC_LEASE_MS 15000

typedef struct {
    char client[17];
    uint32_t expires_ms;
} can_sync_lease_t;

typedef struct {
    char session[17];
    char client[17];
    uint32_t count;
} can_sync_receipt_t;

typedef struct {
    can_msg_entry_t *base;
    uint32_t capacity;
    uint32_t count;
    bool recording;
    uint32_t dropped;
    uint32_t start_ms;
    uint32_t stop_ms;
    bool psram_ok;
    bool full;
    bool protected_record;
    uint32_t readers;
    uint32_t boot_nonce;
    uint32_t generation;
    char session[17];
    char signals_json[CAN_LOG_CONFIG_MAX_BYTES + 1];
    uint32_t ids[CAN_LOG_MAX_IDS];
    size_t id_count;
    uint32_t rx_missed_start;
    uint32_t rx_overrun_start;
    uint32_t rx_lost;
    bool rx_quality_known;
    can_sync_lease_t leases[CAN_LOG_SYNC_CLIENTS];
    can_sync_receipt_t receipts[CAN_LOG_SYNC_CLIENTS];
    uint32_t receipt_next;
    SemaphoreHandle_t mutex;
} can_logger_t;

static can_logger_t s_log;

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

// 所有 *_locked 辅助函数均由调用方持有 s_log.mutex。
static bool session_matches_locked(const char *session)
{
    return session && s_log.session[0] && strcmp(session, s_log.session) == 0;
}

static bool client_valid(const char *client)
{
    if (!client || strnlen(client, 17) != 16) return false;
    for (const char *p = client; *p; p++) {
        if (!isxdigit((unsigned char)*p)) return false;
    }
    return true;
}

static void expire_leases_locked(void)
{
    uint32_t now = now_ms();
    for (size_t i = 0; i < CAN_LOG_SYNC_CLIENTS; i++) {
        if (s_log.leases[i].client[0] &&
            (int32_t)(now - s_log.leases[i].expires_ms) >= 0) {
            s_log.leases[i].client[0] = '\0';
        }
    }
}

static bool other_leases_locked(const char *client)
{
    for (size_t i = 0; i < CAN_LOG_SYNC_CLIENTS; i++) {
        if (s_log.leases[i].client[0] &&
            (!client || strcmp(client, s_log.leases[i].client) != 0)) return true;
    }
    return false;
}

static void drop_lease_locked(const char *client)
{
    for (size_t i = 0; i < CAN_LOG_SYNC_CLIENTS; i++) {
        if (strcmp(client, s_log.leases[i].client) == 0) {
            s_log.leases[i].client[0] = '\0';
        }
    }
}

static bool renew_lease_locked(const char *client)
{
    expire_leases_locked();
    can_sync_lease_t *free_lease = NULL;
    for (size_t i = 0; i < CAN_LOG_SYNC_CLIENTS; i++) {
        can_sync_lease_t *lease = &s_log.leases[i];
        if (strcmp(client, lease->client) == 0) {
            lease->expires_ms = now_ms() + CAN_LOG_SYNC_LEASE_MS;
            return true;
        }
        if (!lease->client[0] && !free_lease) free_lease = lease;
    }
    if (!free_lease) return false;
    memcpy(free_lease->client, client, sizeof(free_lease->client));
    free_lease->expires_ms = now_ms() + CAN_LOG_SYNC_LEASE_MS;
    return true;
}

static bool receipt_matches_locked(const char *session, const char *client, uint32_t count)
{
    if (!session) return false;
    for (size_t i = 0; i < CAN_LOG_SYNC_CLIENTS; i++) {
        const can_sync_receipt_t *receipt = &s_log.receipts[i];
        if (receipt->session[0] && receipt->count == count &&
            strcmp(receipt->session, session) == 0 &&
            strcmp(receipt->client, client) == 0) return true;
    }
    return false;
}

static void save_receipt_locked(const char *client, uint32_t count)
{
    if (receipt_matches_locked(s_log.session, client, count)) return;
    can_sync_receipt_t *receipt = &s_log.receipts[s_log.receipt_next];
    memcpy(receipt->session, s_log.session, sizeof(receipt->session));
    memcpy(receipt->client, client, sizeof(receipt->client));
    receipt->count = count;
    s_log.receipt_next = (s_log.receipt_next + 1) % CAN_LOG_SYNC_CLIENTS;
}

static void update_rx_quality_locked(void)
{
    if (!s_log.rx_quality_known) return;
    twai_status_info_t info;
    if (twai_get_status_info(&info) != ESP_OK) {
        s_log.rx_quality_known = false;
        return;
    }
    // 驱动计数可能被外部重启重置。此时无法确认区间，不能声称完整。
    if (info.rx_missed_count < s_log.rx_missed_start ||
        info.rx_overrun_count < s_log.rx_overrun_start) {
        s_log.rx_quality_known = false;
        return;
    }
    uint64_t lost = (uint64_t)(info.rx_missed_count - s_log.rx_missed_start) +
                    (uint64_t)(info.rx_overrun_count - s_log.rx_overrun_start);
    s_log.rx_lost = lost > UINT32_MAX ? UINT32_MAX : (uint32_t)lost;
}

static void status_locked(can_log_status_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->psram_ok = s_log.psram_ok;
    out->capacity = s_log.capacity;
    out->recording = s_log.recording;
    out->count = s_log.count;
    out->dropped = s_log.dropped;
    out->full = s_log.full;
    out->protected_record = s_log.protected_record;
    out->rx_lost = s_log.rx_lost;
    out->rx_quality_known = s_log.rx_quality_known;
    memcpy(out->session, s_log.session, sizeof(out->session));
    if (s_log.session[0]) {
        out->duration_ms = (s_log.recording ? now_ms() : s_log.stop_ms) - s_log.start_ms;
    }
}

static void stop_locked(void)
{
    if (!s_log.recording) return;
    s_log.recording = false;
    s_log.stop_ms = now_ms();
    update_rx_quality_locked();
}

esp_err_t can_log_init(void)
{
    if (s_log.mutex) return ESP_OK;
    s_log.mutex = xSemaphoreCreateMutex();
    if (!s_log.mutex) return ESP_ERR_NO_MEM;
    s_log.boot_nonce = esp_random();

    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    size_t reserve = CAN_LOG_PSRAM_RESERVE + CAN_LOG_ALLOC_MARGIN;
    size_t budget = free_psram > reserve ? free_psram - reserve : 0;
    size_t allocation = CAN_LOG_PSRAM_SIZE;
    if (allocation > budget) allocation = budget;
    if (allocation > largest) allocation = largest;
    allocation -= allocation % sizeof(can_msg_entry_t);
    if (allocation >= sizeof(can_msg_entry_t) * 1024) {
        s_log.base = heap_caps_malloc(allocation, MALLOC_CAP_SPIRAM);
    }
    if (!s_log.base) {
        ESP_LOGW(TAG, "PSRAM alloc failed, recording disabled (512 KiB reserved)");
        return ESP_OK;
    }
    // 使用请求大小，不把分配器的额外对齐空间计入可写容量。
    s_log.capacity = allocation / sizeof(can_msg_entry_t);
    s_log.psram_ok = true;
    ESP_LOGI(TAG, "PSRAM log buffer: %u bytes, %u entries (entry=%u bytes)",
             (unsigned)(s_log.capacity * sizeof(can_msg_entry_t)),
             (unsigned)s_log.capacity, (unsigned)sizeof(can_msg_entry_t));
    return ESP_OK;
}

static esp_err_t start_record(const char *signals_json, const uint32_t *ids,
                              size_t id_count, bool protect, can_log_status_t *out)
{
    if (!signals_json || !ids || !id_count || id_count > CAN_LOG_MAX_IDS ||
        strnlen(signals_json, CAN_LOG_CONFIG_MAX_BYTES + 1) > CAN_LOG_CONFIG_MAX_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < id_count; i++) {
        if (ids[i] > 0x1FFFFFFFu) return ESP_ERR_INVALID_ARG;
    }
    if (!s_log.mutex || !s_log.psram_ok) return ESP_ERR_NOT_SUPPORTED;

    xSemaphoreTake(s_log.mutex, portMAX_DELAY);
    expire_leases_locked();
    if (s_log.recording || s_log.readers || s_log.protected_record || other_leases_locked(NULL)) {
        xSemaphoreGive(s_log.mutex);
        return ESP_ERR_INVALID_STATE;
    }
    s_log.id_count = 0;
    for (size_t i = 0; i < id_count; i++) {
        bool duplicate = false;
        for (size_t j = 0; j < s_log.id_count; j++) {
            if (s_log.ids[j] == ids[i]) { duplicate = true; break; }
        }
        if (!duplicate) s_log.ids[s_log.id_count++] = ids[i];
    }
    memcpy(s_log.signals_json, signals_json, strlen(signals_json) + 1);
    // 代次回绕时更换随机前缀，避免重新出现旧会话编号。
    if (++s_log.generation == 0) {
        s_log.boot_nonce = esp_random();
        s_log.generation = 1;
    }
    snprintf(s_log.session, sizeof(s_log.session), "%08lx%08lx",
             (unsigned long)s_log.boot_nonce, (unsigned long)s_log.generation);
    s_log.count = 0;
    memset(s_log.leases, 0, sizeof(s_log.leases));
    s_log.dropped = 0;
    s_log.full = false;
    s_log.protected_record = protect;
    s_log.start_ms = now_ms();
    s_log.stop_ms = 0;
    s_log.rx_lost = 0;
    twai_status_info_t info;
    s_log.rx_quality_known = twai_get_status_info(&info) == ESP_OK;
    if (s_log.rx_quality_known) {
        s_log.rx_missed_start = info.rx_missed_count;
        s_log.rx_overrun_start = info.rx_overrun_count;
    }
    s_log.recording = true;
    status_locked(out);
    xSemaphoreGive(s_log.mutex);
    ESP_LOGI(TAG, "Recording started (capacity=%u)", (unsigned)s_log.capacity);
    return ESP_OK;
}

esp_err_t can_log_start_config(const char *signals_json, const uint32_t *ids,
                               size_t id_count, can_log_status_t *out)
{
    return start_record(signals_json, ids, id_count, true, out);
}

esp_err_t can_log_start(void)
{
    const uint32_t ids[] = {SIG_ID_MOTOR_DRIVE, SIG_ID_BUS_VI};
    return start_record("[]", ids, sizeof(ids) / sizeof(ids[0]), false, NULL);
}

esp_err_t can_log_stop_session(const char *session, can_log_status_t *out)
{
    if (!s_log.mutex || !s_log.psram_ok) return ESP_ERR_NOT_SUPPORTED;
    xSemaphoreTake(s_log.mutex, portMAX_DELAY);
    if (!session_matches_locked(session)) {
        xSemaphoreGive(s_log.mutex);
        return ESP_ERR_NOT_FOUND;
    }
    stop_locked();
    status_locked(out);
    xSemaphoreGive(s_log.mutex);
    return ESP_OK;
}

esp_err_t can_log_stop(void)
{
    if (!s_log.mutex || !s_log.psram_ok) return ESP_ERR_NOT_SUPPORTED;
    xSemaphoreTake(s_log.mutex, portMAX_DELAY);
    stop_locked();
    xSemaphoreGive(s_log.mutex);
    return ESP_OK;
}

esp_err_t can_log_clear(void)
{
    if (!s_log.mutex || !s_log.psram_ok) return ESP_ERR_NOT_SUPPORTED;
    xSemaphoreTake(s_log.mutex, portMAX_DELAY);
    expire_leases_locked();
    if (s_log.recording || s_log.readers || s_log.protected_record || other_leases_locked(NULL)) {
        xSemaphoreGive(s_log.mutex);
        return ESP_ERR_INVALID_STATE;
    }
    s_log.count = 0;
    memset(s_log.leases, 0, sizeof(s_log.leases));
    s_log.dropped = 0;
    s_log.start_ms = 0;
    s_log.stop_ms = 0;
    s_log.full = false;
    s_log.rx_lost = 0;
    s_log.rx_quality_known = false;
    s_log.session[0] = '\0';
    s_log.signals_json[0] = '\0';
    s_log.id_count = 0;
    xSemaphoreGive(s_log.mutex);
    return ESP_OK;
}

bool can_log_is_recording(void)
{
    if (!s_log.mutex) return false;
    xSemaphoreTake(s_log.mutex, portMAX_DELAY);
    bool recording = s_log.recording;
    xSemaphoreGive(s_log.mutex);
    return recording;
}

void can_log_get_status(can_log_status_t *out)
{
    if (!out) return;
    if (!s_log.mutex) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_log.mutex, portMAX_DELAY);
    if (s_log.recording) update_rx_quality_locked();
    status_locked(out);
    xSemaphoreGive(s_log.mutex);
}

esp_err_t can_log_get_buffer(can_msg_entry_t **base, uint32_t *count)
{
    if (!base || !count) return ESP_ERR_INVALID_ARG;
    if (!s_log.mutex || !s_log.psram_ok) return ESP_ERR_NOT_SUPPORTED;
    xSemaphoreTake(s_log.mutex, portMAX_DELAY);
    *base = s_log.base;
    *count = s_log.count;
    xSemaphoreGive(s_log.mutex);
    return ESP_OK;
}

esp_err_t can_log_copy_config(const char *session, char *out, size_t size)
{
    if (!out || !size) return ESP_ERR_INVALID_ARG;
    if (!s_log.mutex || !s_log.psram_ok) return ESP_ERR_NOT_SUPPORTED;
    xSemaphoreTake(s_log.mutex, portMAX_DELAY);
    esp_err_t ret = ESP_OK;
    if (!session_matches_locked(session)) ret = ESP_ERR_NOT_FOUND;
    else if (strlen(s_log.signals_json) + 1 > size) ret = ESP_ERR_INVALID_SIZE;
    else memcpy(out, s_log.signals_json, strlen(s_log.signals_json) + 1);
    xSemaphoreGive(s_log.mutex);
    return ret;
}

static esp_err_t read_record(const char *session, const char *client, uint32_t offset,
                             can_msg_entry_t *out, uint32_t max_entries,
                             uint32_t *out_count, can_log_status_t *status)
{
    if (!out_count || (!out && max_entries)) return ESP_ERR_INVALID_ARG;
    *out_count = 0;
    if (!s_log.mutex || !s_log.psram_ok) return ESP_ERR_NOT_SUPPORTED;
    xSemaphoreTake(s_log.mutex, portMAX_DELAY);
    esp_err_t ret = ESP_OK;
    if (!session_matches_locked(session)) ret = ESP_ERR_NOT_FOUND;
    else if (offset > s_log.count) ret = ESP_ERR_INVALID_ARG;
    else {
        uint32_t count = s_log.count - offset;
        if (count > max_entries) count = max_entries;
        if (count > CAN_LOG_READ_MAX_ENTRIES) count = CAN_LOG_READ_MAX_ENTRIES;
        // 包括最后一批和末尾空读，均保留租约，直到客户端处理成功后显式 ACK。
        if (client && !renew_lease_locked(client)) {
            ret = ESP_ERR_INVALID_STATE;
        }
        if (ret == ESP_OK) {
            if (count) memcpy(out, &s_log.base[offset], count * sizeof(*out));
            *out_count = count;
            status_locked(status);
        }
    }
    xSemaphoreGive(s_log.mutex);
    return ret;
}

esp_err_t can_log_read(const char *session, uint32_t offset, can_msg_entry_t *out,
                      uint32_t max_entries, uint32_t *out_count,
                      can_log_status_t *status)
{
    return read_record(session, NULL, offset, out, max_entries, out_count, status);
}

esp_err_t can_log_read_client(const char *session, const char *client, uint32_t offset,
                             can_msg_entry_t *out, uint32_t max_entries,
                             uint32_t *out_count, can_log_status_t *status)
{
    if (out_count) *out_count = 0;
    if (!client_valid(client)) return ESP_ERR_INVALID_ARG;
    return read_record(session, client, offset, out, max_entries, out_count, status);
}

esp_err_t can_log_sync_done(const char *session, const char *client, uint32_t count)
{
    if (!client_valid(client)) return ESP_ERR_INVALID_ARG;
    if (!s_log.mutex || !s_log.psram_ok) return ESP_ERR_NOT_SUPPORTED;
    xSemaphoreTake(s_log.mutex, portMAX_DELAY);
    esp_err_t ret = ESP_OK;
    if (!session_matches_locked(session)) {
        // 已成功处理的旧 ACK 可重试，但绝不修改新录制或新客户端租约。
        ret = receipt_matches_locked(session, client, count) ? ESP_OK : ESP_ERR_NOT_FOUND;
    }
    else if (s_log.recording || count != s_log.count) ret = ESP_ERR_INVALID_STATE;
    else {
        expire_leases_locked();
        save_receipt_locked(client, count);
        drop_lease_locked(client);
    }
    xSemaphoreGive(s_log.mutex);
    return ret;
}

esp_err_t can_log_acquire(const char *session, can_log_status_t *out)
{
    if (!s_log.mutex || !s_log.psram_ok) return ESP_ERR_NOT_SUPPORTED;
    xSemaphoreTake(s_log.mutex, portMAX_DELAY);
    esp_err_t ret = ESP_OK;
    if (!session_matches_locked(session)) ret = ESP_ERR_NOT_FOUND;
    else if (s_log.recording || s_log.readers == UINT32_MAX) ret = ESP_ERR_INVALID_STATE;
    else { s_log.readers++; status_locked(out); }
    xSemaphoreGive(s_log.mutex);
    return ret;
}

void can_log_release(const char *session)
{
    if (!s_log.mutex) return;
    xSemaphoreTake(s_log.mutex, portMAX_DELAY);
    if (session_matches_locked(session) && s_log.readers) s_log.readers--;
    xSemaphoreGive(s_log.mutex);
}

esp_err_t can_log_unprotect(const char *session)
{
    if (!s_log.mutex || !s_log.psram_ok) return ESP_ERR_NOT_SUPPORTED;
    xSemaphoreTake(s_log.mutex, portMAX_DELAY);
    expire_leases_locked();
    esp_err_t ret = ESP_OK;
    if (!session_matches_locked(session)) ret = ESP_ERR_NOT_FOUND;
    else if (s_log.recording || s_log.readers || other_leases_locked(NULL)) ret = ESP_ERR_INVALID_STATE;
    else s_log.protected_record = false;
    xSemaphoreGive(s_log.mutex);
    return ret;
}

esp_err_t can_log_unprotect_client(const char *session, const char *client)
{
    if (!client_valid(client)) return ESP_ERR_INVALID_ARG;
    if (!s_log.mutex || !s_log.psram_ok) return ESP_ERR_NOT_SUPPORTED;
    xSemaphoreTake(s_log.mutex, portMAX_DELAY);
    expire_leases_locked();
    esp_err_t ret = ESP_OK;
    if (!session_matches_locked(session)) ret = ESP_ERR_NOT_FOUND;
    else if (s_log.recording || s_log.readers || other_leases_locked(client)) ret = ESP_ERR_INVALID_STATE;
    else {
        drop_lease_locked(client);
        s_log.protected_record = false;
    }
    xSemaphoreGive(s_log.mutex);
    return ret;
}

void can_log_write(const can_msg_entry_t *entry)
{
    // RX 任务先于记录器初始化启动，不能访问未初始化的锁。
    if (!entry || !s_log.mutex || !s_log.psram_ok) return;
    xSemaphoreTake(s_log.mutex, portMAX_DELAY);
    if (s_log.recording) {
        bool wanted = false;
        for (size_t i = 0; i < s_log.id_count; i++) {
            if (entry->id == s_log.ids[i]) { wanted = true; break; }
        }
        if (wanted) {
            if (s_log.count < s_log.capacity) {
                s_log.base[s_log.count++] = *entry;
            }
            // 满时立刻停止。最后一帧已保存，后续不覆盖任何已有数据。
            if (s_log.count == s_log.capacity) {
                s_log.full = true;
                stop_locked();
            }
        }
    }
    xSemaphoreGive(s_log.mutex);
}
