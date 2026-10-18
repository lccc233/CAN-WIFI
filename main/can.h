#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/twai.h"

// TWAI 引脚
#define TWAI_TX_GPIO  5
#define TWAI_RX_GPIO  4

// 频率统计最多跟踪的 ID 数量
#define CAN_FREQ_MAX_IDS  128

// 监控历史：每个 ID 独立保留最新 10 帧，ID 槽满时淘汰最久未更新的 ID。
#define CAN_MONITOR_MAX_IDS     CAN_FREQ_MAX_IDS
#define CAN_MONITOR_PER_ID      10
#define CAN_MONITOR_CAPACITY    (CAN_MONITOR_MAX_IDS * CAN_MONITOR_PER_ID)

// 独立曲线增量通道：设备仅作传输缓冲，浏览器保存完整时间窗口。
#define CAN_LIVE_CAPACITY       4096
#define CAN_LIVE_BATCH          512

// 单条 CAN 消息记录。
// packed 消除对齐 padding：20B -> 18B（PSRAM 容量 +18%）。
// u32 字段落位恰在 0/4 偏移（data 后无补位），无未对齐内存访问 penalty。
typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;
    uint32_t id;
    uint8_t  data[8];
    uint8_t  dlc;
    uint8_t  extended;   // 原 bool；1=扩展帧，0=标准帧
} can_msg_entry_t;

// 单个 ID 的接收频率统计
typedef struct {
    uint32_t id;
    uint32_t rate_x10;    // 发送频率，单位 0.1 条/秒（除以 10 得 条/秒）
    uint32_t last_msg_ms; // 该 ID 最近一次收到消息的时间戳
} can_id_freq_t;

typedef struct {
    uint64_t from, next, head, lost;
    bool ready;
} can_live_meta_t;

// 公共 API
esp_err_t can_init(void);
esp_err_t can_send_message(uint32_t id, bool extended, uint8_t dlc, const uint8_t *data);
void      can_get_snapshot(can_msg_entry_t *out, uint32_t max_entries,
                           uint32_t *out_count, uint32_t *out_total);
// 快照按接收序号从旧到新返回；容量不足时返回其中最新的帧。
// seq 为启动以来的接收序号；清空监控不重置 seq。
void      can_get_snapshot_seq(can_msg_entry_t *out, uint32_t *seq,
                               uint32_t max_entries, uint32_t *out_count,
                               uint32_t *out_total);
uint32_t  can_get_boot_id(void);
void      can_clear_ring(void);
void      can_get_live(uint64_t from, bool reset, can_msg_entry_t *out,
                       uint32_t max_entries, uint32_t *out_count, can_live_meta_t *meta);
void      can_get_live_filtered(uint64_t from, bool reset, const uint32_t *ids, uint32_t id_count,
                                can_msg_entry_t *out, uint64_t *seq, uint32_t max_entries,
                                uint32_t *out_count, can_live_meta_t *meta);

// 获取各 ID 的接收频率统计，返回写入条数（最多 max_ids 条）
int       can_get_id_freqs(can_id_freq_t *out, int max_ids);
