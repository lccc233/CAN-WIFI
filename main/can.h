#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/twai.h"

// TWAI 引脚
#define TWAI_TX_GPIO  5
#define TWAI_RX_GPIO  4

// 环形缓冲大小（必须是 2 的幂）
#define CAN_RX_RING_SIZE  128

// 频率统计最多跟踪的 ID 数量
#define CAN_FREQ_MAX_IDS  128

// 单条 CAN 消息记录
typedef struct {
    uint32_t timestamp_ms;
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[8];
    bool     extended;
} can_msg_entry_t;

// 环形缓冲
typedef struct {
    can_msg_entry_t entries[CAN_RX_RING_SIZE];
    volatile uint32_t head;
    volatile uint32_t count;
    SemaphoreHandle_t mutex;
} can_rx_ring_t;

// 单个 ID 的接收频率统计
typedef struct {
    uint32_t id;
    uint32_t rate_x10;    // 发送频率，单位 0.1 条/秒（除以 10 得 条/秒）
    uint32_t last_msg_ms; // 该 ID 最近一次收到消息的时间戳
} can_id_freq_t;

// 公共 API
esp_err_t can_init(void);
esp_err_t can_send_message(uint32_t id, bool extended, uint8_t dlc, const uint8_t *data);
void      can_get_snapshot(can_msg_entry_t *out, uint32_t max_entries,
                           uint32_t *out_count, uint32_t *out_total);
uint32_t  can_get_total_received(void);
void      can_clear_ring(void);

// 获取各 ID 的接收频率统计，返回写入条数（最多 max_ids 条）
int       can_get_id_freqs(can_id_freq_t *out, int max_ids);
