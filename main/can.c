#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "driver/twai.h"
#include "can.h"
#include "can_logger.h"

static const char *TAG = "can";

typedef struct {
    can_msg_entry_t entries[CAN_MONITOR_PER_ID];
    uint32_t seq[CAN_MONITOR_PER_ID];
    uint8_t head;   // 下次写入的位置（10 不是 2 的幂，使用取模）
    uint8_t count;
} can_monitor_id_t;

static struct {
    can_monitor_id_t ids[CAN_MONITOR_MAX_IDS];
    uint32_t id_count;
    uint32_t head;  // 启动以来的接收序号，清空监控后仍递增
    uint32_t count;
    SemaphoreHandle_t mutex;
} g_ring;
static struct {
    can_msg_entry_t *entries;
    uint64_t head;
    uint32_t count;
} s_live;
static SemaphoreHandle_t g_send_mutex;
static uint32_t s_boot_id;

// ---- 每个 ID 的接收频率统计 ----

typedef struct {
    uint32_t id;
    uint32_t window_start_ms;   // 当前统计周期起点
    uint32_t count;             // 当前周期内收到的消息数
    uint32_t rate_x10;          // 上一周期计算出的频率（单位 0.1 条/秒）
    uint32_t last_msg_ms;       // 最近一次收到该 ID 消息的时间
} can_freq_stat_t;

static can_freq_stat_t s_freq[CAN_FREQ_MAX_IDS];
static int s_freq_count = 0;
static SemaphoreHandle_t g_freq_mutex;

// ---- 前向声明 ----
static void can_rx_task(void *arg);
static void can_alert_task(void *arg);
static void twai_print_status(void);

// ---- 每 ID 监控历史 ----

void can_clear_ring(void)
{
    if (!g_ring.mutex || !g_freq_mutex) return;
    xSemaphoreTake(g_ring.mutex, portMAX_DELAY);
    // 保留启动以来的帧序号，避免清空后前端误判为旧帧。
    g_ring.id_count = 0;
    g_ring.count = 0;
    xSemaphoreGive(g_ring.mutex);

    xSemaphoreTake(g_freq_mutex, portMAX_DELAY);
    memset(s_freq, 0, sizeof(s_freq));
    s_freq_count = 0;
    xSemaphoreGive(g_freq_mutex);
}

void can_get_snapshot(can_msg_entry_t *out, uint32_t max_entries,
                      uint32_t *out_count, uint32_t *out_total)
{
    can_get_snapshot_seq(out, NULL, max_entries, out_count, out_total);
}

uint32_t can_get_boot_id(void)
{
    return s_boot_id;
}

void can_get_snapshot_seq(can_msg_entry_t *out, uint32_t *seq,
                          uint32_t max_entries, uint32_t *out_count,
                          uint32_t *out_total)
{
    if (!out_count || !out_total) return;
    *out_count = 0;
    *out_total = 0;
    if (!g_ring.mutex || (!out && max_entries)) return;
    xSemaphoreTake(g_ring.mutex, portMAX_DELAY);
    uint8_t remaining[CAN_MONITOR_MAX_IDS];
    uint32_t available = 0;
    for (uint32_t i = 0; i < g_ring.id_count; i++) {
        remaining[i] = g_ring.ids[i].count;
        available += remaining[i];
    }
    uint32_t to_copy = (available < max_entries) ? available : max_entries;
    // 每个 ID 的小环从新到旧归并，倒序填入输出；相同时间戳仍按 seq 排序。
    for (uint32_t n = to_copy; n > 0; n--) {
        uint32_t selected = 0, selected_pos = 0, newest_age = UINT32_MAX;
        for (uint32_t i = 0; i < g_ring.id_count; i++) {
            if (!remaining[i]) continue;
            can_monitor_id_t *bucket = &g_ring.ids[i];
            uint32_t pos = (bucket->head + CAN_MONITOR_PER_ID
                            - bucket->count + remaining[i] - 1) % CAN_MONITOR_PER_ID;
            uint32_t age = g_ring.head - bucket->seq[pos];
            if (age < newest_age) {
                selected = i;
                selected_pos = pos;
                newest_age = age;
            }
        }
        out[n - 1] = g_ring.ids[selected].entries[selected_pos];
        if (seq) seq[n - 1] = g_ring.ids[selected].seq[selected_pos];
        remaining[selected]--;
    }
    *out_count = to_copy;
    *out_total = g_ring.count;
    xSemaphoreGive(g_ring.mutex);
}

// 只覆盖同 ID 的旧帧；新 ID 在槽位满时替换最久未收到报文的 ID。
static void can_monitor_record(const can_msg_entry_t *entry)
{
    xSemaphoreTake(g_ring.mutex, portMAX_DELAY);
    can_monitor_id_t *bucket = NULL;
    for (uint32_t i = 0; i < g_ring.id_count; i++) {
        if (g_ring.ids[i].entries[0].id == entry->id) {
            bucket = &g_ring.ids[i];
            break;
        }
    }
    if (!bucket) {
        if (g_ring.id_count < CAN_MONITOR_MAX_IDS) {
            bucket = &g_ring.ids[g_ring.id_count++];
        } else {
            uint32_t oldest_age = 0;
            for (uint32_t i = 0; i < g_ring.id_count; i++) {
                can_monitor_id_t *candidate = &g_ring.ids[i];
                uint32_t last = (candidate->head + CAN_MONITOR_PER_ID - 1) % CAN_MONITOR_PER_ID;
                uint32_t age = g_ring.head - candidate->seq[last];
                if (!bucket || age > oldest_age) {
                    bucket = candidate;
                    oldest_age = age;
                }
            }
        }
        bucket->head = 0;
        bucket->count = 0;
    }
    bucket->entries[bucket->head] = *entry;
    bucket->seq[bucket->head] = g_ring.head++;
    bucket->head = (bucket->head + 1) % CAN_MONITOR_PER_ID;
    if (bucket->count < CAN_MONITOR_PER_ID) bucket->count++;
    if (g_ring.count < UINT32_MAX) g_ring.count++;
    if (s_live.entries) {
        s_live.entries[s_live.head % CAN_LIVE_CAPACITY] = *entry;
        s_live.head++;
        if (s_live.count < CAN_LIVE_CAPACITY) s_live.count++;
    }
    xSemaphoreGive(g_ring.mutex);
}

// 清空报文列表不清除此通道；多个浏览器各自持有游标，不消耗彼此的数据。
void can_get_live(uint64_t from, bool reset, can_msg_entry_t *out,
                  uint32_t max_entries, uint32_t *out_count, can_live_meta_t *meta)
{
    can_get_live_filtered(from, reset, NULL, 0, out, NULL, max_entries, out_count, meta);
}

void can_get_live_filtered(uint64_t from, bool reset, const uint32_t *ids, uint32_t id_count,
                           can_msg_entry_t *out, uint64_t *seq, uint32_t max_entries,
                           uint32_t *out_count, can_live_meta_t *meta)
{
    *out_count = 0;
    memset(meta, 0, sizeof(*meta));
    if (!g_ring.mutex) return;
    xSemaphoreTake(g_ring.mutex, portMAX_DELAY);
    meta->ready = s_live.entries != NULL;
    meta->head = s_live.head;
    uint64_t oldest = s_live.head - s_live.count;
    if (reset || from > s_live.head) from = oldest;
    if (from < oldest) { meta->lost = oldest - from; from = oldest; }
    uint32_t count = 0;
    uint64_t cursor = from;
    while (cursor < s_live.head && count < max_entries) {
        const can_msg_entry_t *entry = &s_live.entries[cursor % CAN_LIVE_CAPACITY];
        bool match = ids == NULL;
        for (uint32_t i = 0; !match && i < id_count; i++) match = ids[i] == entry->id;
        if (match) {
            out[count] = *entry;
            if (seq) seq[count] = cursor;
            count++;
        }
        cursor++;
    }
    meta->from = from;
    meta->next = cursor;
    *out_count = count;
    xSemaphoreGive(g_ring.mutex);
}

// ---- 每 ID 频率统计 ----

// 记录一条收到的消息。使用滚动 1s 周期：周期内累加计数，
// 周期结束时按实际时长计算频率（条/秒 * 10）并缓存。
static void can_freq_record(uint32_t id, uint32_t now_ms)
{
    xSemaphoreTake(g_freq_mutex, portMAX_DELAY);

    // 查找已有条目
    can_freq_stat_t *st = NULL;
    for (int i = 0; i < s_freq_count; i++) {
        if (s_freq[i].id == id) {
            st = &s_freq[i];
            break;
        }
    }

    // 新 ID：有空闲槽则新建，否则淘汰最久未更新的 ID
    if (!st) {
        if (s_freq_count < CAN_FREQ_MAX_IDS) {
            st = &s_freq[s_freq_count++];
        } else {
            st = &s_freq[0];
            for (int i = 1; i < s_freq_count; i++) {
                if (s_freq[i].last_msg_ms < st->last_msg_ms) {
                    st = &s_freq[i];
                }
            }
        }
        st->id = id;
        st->window_start_ms = now_ms;
        st->count = 0;
        st->rate_x10 = 0;
    }

    uint32_t elapsed = now_ms - st->window_start_ms;
    if (elapsed >= 1000) {
        // 周期结束：用实际时长计算频率并缓存，再开启新周期
        st->rate_x10 = (elapsed > 0 && st->count > 0)
                     ? (uint32_t)(((uint64_t)st->count * 10000u) / elapsed)
                     : 0;
        st->window_start_ms = now_ms;
        st->count = 1;
    } else {
        st->count++;
    }
    st->last_msg_ms = now_ms;

    xSemaphoreGive(g_freq_mutex);
}

int can_get_id_freqs(can_id_freq_t *out, int max_ids)
{
    xSemaphoreTake(g_freq_mutex, portMAX_DELAY);
    int n = (s_freq_count < max_ids) ? s_freq_count : max_ids;
    for (int i = 0; i < n; i++) {
        out[i].id = s_freq[i].id;
        out[i].rate_x10 = s_freq[i].rate_x10;
        out[i].last_msg_ms = s_freq[i].last_msg_ms;
    }
    xSemaphoreGive(g_freq_mutex);
    return n;
}

// ---- CAN 发送 ----

esp_err_t can_send_message(uint32_t id, bool extended, uint8_t dlc, const uint8_t *data)
{
    if (dlc > 8) dlc = 8;   // 钳位必须在填 data_length_code 之前
    if (extended ? (id > 0x1FFFFFFFu) : (id > 0x7FFu)) {
        return ESP_ERR_INVALID_ARG;   // 扩展帧 29 位 / 标准帧 11 位
    }
    twai_message_t msg = {
        .identifier = id,
        .extd = extended ? 1 : 0,
        .rtr = 0,
        .data_length_code = dlc,
    };
    memcpy(msg.data, data, dlc);

    xSemaphoreTake(g_send_mutex, portMAX_DELAY);
    esp_err_t ret = twai_transmit(&msg, pdMS_TO_TICKS(100));
    xSemaphoreGive(g_send_mutex);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "CAN TX failed: %s (ID=0x%lX)", esp_err_to_name(ret), (unsigned long)id);
    }
    return ret;
}

// ---- CAN 接收任务 ----

static void can_rx_task(void *arg)
{
    twai_message_t rx_msg;
    ESP_LOGI(TAG, "CAN RX task started");

    while (1) {
        if (twai_receive(&rx_msg, pdMS_TO_TICKS(100)) == ESP_OK) {
            // 经典帧 DLC 9~15 在线路上仍只有 8 字节数据（CAN 规范按 8 处理），
            // 驱动会原样上报 DLC 而只填 8 字节，必须钳位防下方 memcpy 越界
            if (rx_msg.data_length_code > 8) rx_msg.data_length_code = 8;
            uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            can_msg_entry_t entry = {
                .timestamp_ms = now_ms,
                .id = rx_msg.identifier,
                .dlc = rx_msg.rtr ? 0 : rx_msg.data_length_code,
                .extended = rx_msg.extd,
            };
            if (!rx_msg.rtr) {
                memcpy(entry.data, rx_msg.data, entry.dlc);
                // 记录优先于监控和频率统计，且不引用可能被清空/覆盖的环形槽。
                can_log_write(&entry);
            }
            can_monitor_record(&entry);
            can_freq_record(rx_msg.identifier, now_ms);
        }
    }
}

// ---- TWAI 告警处理任务 ----

// 告警日志去抖：同一 alert 位（bit0-31）在 1 秒窗口内只打印一次，
// 防止总线上高噪声时日志刷屏占用 CPU。恢复动作本身不受去抖影响。
static bool can_alert_log_ok(int bit)
{
    static uint32_t s_last_ms[32];
    if (bit < 0 || bit > 31) return true;
    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    bool ok = (now - s_last_ms[bit]) >= 1000;
    if (ok) s_last_ms[bit] = now;
    return ok;
}

static void can_alert_task(void *arg)
{
    ESP_LOGI(TAG, "TWAI alert task started");
    while (1) {
        uint32_t alerts;
        esp_err_t ret = twai_read_alerts(&alerts, pdMS_TO_TICKS(500));
        if (ret == ESP_OK && alerts != 0) {
            if (alerts & TWAI_ALERT_BUS_OFF) {
                if (can_alert_log_ok(__builtin_ffs(TWAI_ALERT_BUS_OFF) - 1)) {
                    ESP_LOGW(TAG, "BUS-OFF detected! Initiating recovery...");
                }
                twai_initiate_recovery();
            }
            if (alerts & TWAI_ALERT_RECOVERY_IN_PROGRESS) {
                if (can_alert_log_ok(__builtin_ffs(TWAI_ALERT_RECOVERY_IN_PROGRESS) - 1)) {
                    ESP_LOGI(TAG, "Bus-off recovery in progress...");
                }
            }
            if (alerts & TWAI_ALERT_BUS_RECOVERED) {
                ESP_LOGI(TAG, "Bus recovered! Restarting...");
                twai_start();
                twai_print_status();
            }
            if (alerts & TWAI_ALERT_ABOVE_ERR_WARN) {
                if (can_alert_log_ok(__builtin_ffs(TWAI_ALERT_ABOVE_ERR_WARN) - 1)) {
                    ESP_LOGW(TAG, "Error-warning level exceeded");
                }
            }
            if (alerts & TWAI_ALERT_ERR_PASS) {
                if (can_alert_log_ok(__builtin_ffs(TWAI_ALERT_ERR_PASS) - 1)) {
                    ESP_LOGW(TAG, "Entered error-passive state");
                }
            }
            if (alerts & TWAI_ALERT_BUS_ERROR) {
                if (can_alert_log_ok(__builtin_ffs(TWAI_ALERT_BUS_ERROR) - 1)) {
                    ESP_LOGW(TAG, "Bus error detected");
                }
            }
            if (alerts & TWAI_ALERT_TX_FAILED) {
                if (can_alert_log_ok(__builtin_ffs(TWAI_ALERT_TX_FAILED) - 1)) {
                    ESP_LOGW(TAG, "TX failed");
                }
            }
        }
    }
}

// ---- 状态打印 ----

static void twai_print_status(void)
{
    twai_status_info_t status;
    if (twai_get_status_info(&status) == ESP_OK) {
        const char *state_str[] = {"STOPPED", "RUNNING", "BUS_OFF", "RECOVERING"};
        ESP_LOGI(TAG, "TWAI: %s | TXq=%lu RXq=%lu | TEC=%lu REC=%lu",
                 state_str[status.state],
                 (unsigned long)status.msgs_to_tx, (unsigned long)status.msgs_to_rx,
                 (unsigned long)status.tx_error_counter, (unsigned long)status.rx_error_counter);
    }
}

// ---- 初始化 ----

esp_err_t can_init(void)
{
    g_ring.id_count = 0;
    g_ring.head = 0;
    g_ring.count = 0;
    s_boot_id = esp_random();
    g_ring.mutex = xSemaphoreCreateMutex();
    g_send_mutex = xSemaphoreCreateMutex();
    g_freq_mutex = xSemaphoreCreateMutex();
    s_live.entries = heap_caps_malloc(CAN_LIVE_CAPACITY * sizeof(can_msg_entry_t),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_live.entries) ESP_LOGW(TAG, "Live curve transport unavailable: no PSRAM buffer");

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TWAI_TX_GPIO, TWAI_RX_GPIO, TWAI_MODE_NORMAL);
    g_config.alerts_enabled = TWAI_ALERT_ALL;
    g_config.tx_queue_len = 10;
    // 监控快照/曲线/录制并发时吸收短时调度延迟，避免 20 帧队列溢出。
    g_config.rx_queue_len = 128;

    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    gpio_set_pull_mode(TWAI_RX_GPIO, GPIO_PULLUP_ONLY);
    ESP_LOGI(TAG, "TWAI driver installed (250 kbps, NORMAL mode, TX=GPIO%d, RX=GPIO%d)",
             TWAI_TX_GPIO, TWAI_RX_GPIO);

    ESP_ERROR_CHECK(twai_start());
    ESP_LOGI(TAG, "TWAI started");

    xTaskCreate(can_rx_task, "can_rx", 4096, NULL, 6, NULL);
    xTaskCreate(can_alert_task, "can_alert", 3072, NULL, 5, NULL);

    twai_print_status();

    return ESP_OK;
}
