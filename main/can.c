#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/twai.h"
#include "can.h"
#include "can_logger.h"
#include "signal_decode.h"

static const char *TAG = "can";

static can_rx_ring_t g_ring;
static SemaphoreHandle_t g_send_mutex;

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

// ---- 环形缓冲操作 ----

void can_clear_ring(void)
{
    xSemaphoreTake(g_ring.mutex, portMAX_DELAY);
    g_ring.head = 0;
    g_ring.count = 0;
    xSemaphoreGive(g_ring.mutex);

    xSemaphoreTake(g_freq_mutex, portMAX_DELAY);
    memset(s_freq, 0, sizeof(s_freq));
    s_freq_count = 0;
    xSemaphoreGive(g_freq_mutex);
}

uint32_t can_get_total_received(void)
{
    return g_ring.count;
}

void can_get_snapshot(can_msg_entry_t *out, uint32_t max_entries,
                      uint32_t *out_count, uint32_t *out_total)
{
    xSemaphoreTake(g_ring.mutex, portMAX_DELAY);
    uint32_t total = g_ring.count;
    uint32_t available = (total < CAN_RX_RING_SIZE) ? total : CAN_RX_RING_SIZE;
    uint32_t to_copy = (available < max_entries) ? available : max_entries;
    uint32_t start = (g_ring.head - to_copy) & (CAN_RX_RING_SIZE - 1);
    for (uint32_t i = 0; i < to_copy; i++) {
        out[i] = g_ring.entries[(start + i) & (CAN_RX_RING_SIZE - 1)];
    }
    *out_count = to_copy;
    *out_total = total;
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
    twai_message_t msg = {
        .identifier = id,
        .extd = extended ? 1 : 0,
        .rtr = 0,
        .data_length_code = dlc,
    };
    if (dlc > 8) dlc = 8;
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
            uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            uint32_t idx = g_ring.head & (CAN_RX_RING_SIZE - 1);
            g_ring.entries[idx].timestamp_ms = now_ms;
            g_ring.entries[idx].id = rx_msg.identifier;
            g_ring.entries[idx].dlc = rx_msg.data_length_code;
            g_ring.entries[idx].extended = rx_msg.extd;
            memset(g_ring.entries[idx].data, 0, 8);
            memcpy(g_ring.entries[idx].data, rx_msg.data, rx_msg.data_length_code);
            g_ring.head++;
            g_ring.count++;
            can_freq_record(rx_msg.identifier, now_ms);
            can_log_write(&g_ring.entries[idx]);

            // 实时电压/电流显示缓冲（曲线页数据源）
            if (g_ring.entries[idx].id == SIG_ID_BUS_VI) {
                sig_bus_vi_t vi;
                sig_decode_bus_vi(g_ring.entries[idx].data, g_ring.entries[idx].dlc, &vi);
                if (vi.valid) {
                    sig_vi_push(g_ring.entries[idx].timestamp_ms, &vi);
                }
            }
        }
    }
}

// ---- TWAI 告警处理任务 ----

static void can_alert_task(void *arg)
{
    ESP_LOGI(TAG, "TWAI alert task started");
    while (1) {
        uint32_t alerts;
        esp_err_t ret = twai_read_alerts(&alerts, pdMS_TO_TICKS(500));
        if (ret == ESP_OK && alerts != 0) {
            if (alerts & TWAI_ALERT_BUS_OFF) {
                ESP_LOGW(TAG, "BUS-OFF detected! Initiating recovery...");
                twai_initiate_recovery();
            }
            if (alerts & TWAI_ALERT_RECOVERY_IN_PROGRESS) {
                ESP_LOGI(TAG, "Bus-off recovery in progress...");
            }
            if (alerts & TWAI_ALERT_BUS_RECOVERED) {
                ESP_LOGI(TAG, "Bus recovered! Restarting...");
                twai_start();
                twai_print_status();
            }
            if (alerts & TWAI_ALERT_ABOVE_ERR_WARN) {
                ESP_LOGW(TAG, "Error-warning level exceeded");
            }
            if (alerts & TWAI_ALERT_ERR_PASS) {
                ESP_LOGW(TAG, "Entered error-passive state");
            }
            if (alerts & TWAI_ALERT_BUS_ERROR) {
                ESP_LOGW(TAG, "Bus error detected");
            }
            if (alerts & TWAI_ALERT_TX_FAILED) {
                ESP_LOGW(TAG, "TX failed");
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
    g_ring.head = 0;
    g_ring.count = 0;
    g_ring.mutex = xSemaphoreCreateMutex();
    g_send_mutex = xSemaphoreCreateMutex();
    g_freq_mutex = xSemaphoreCreateMutex();

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TWAI_TX_GPIO, TWAI_RX_GPIO, TWAI_MODE_NORMAL);
    g_config.alerts_enabled = TWAI_ALERT_ALL;
    g_config.tx_queue_len = 10;
    g_config.rx_queue_len = 20;

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
