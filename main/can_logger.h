#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "can.h"

// 单条录制状态（用于网页显示）
typedef struct {
    bool     recording;   // 是否正在录制
    uint32_t count;       // 已录制的条数
    uint32_t capacity;    // 缓冲总容量（条）
    uint32_t dropped;     // 因缓冲满而丢弃的条数
    uint32_t duration_ms; // 录制持续时长（从开始到停止）
    bool     psram_ok;    // PSRAM 缓冲是否分配成功
} can_log_status_t;

// 初始化：从 PSRAM 分配记录缓冲（失败不致命，录制功能禁用）
esp_err_t can_log_init(void);

// 开始录制（清空已有数据，重新计时）
esp_err_t can_log_start(void);

// 停止录制（保留已录数据，可继续导出）
esp_err_t can_log_stop(void);

// 清空录制数据并停止录制
esp_err_t can_log_clear(void);

// 查询录制状态
void can_log_get_status(can_log_status_t *out);

// 获取录制缓冲区基址及当前条数（供导出模块直接分块读取）
// 注意：count 是动态快照，录制仍在进行时会持续增长
esp_err_t can_log_get_buffer(can_msg_entry_t **base, uint32_t *count);

// 内部使用：录制模式下写入一条消息（由 can_rx_task 调用）
// 录满自动停止录制（不覆盖不阻塞）
void can_log_write(const can_msg_entry_t *entry);
