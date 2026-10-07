#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "can.h"

#define CAN_LOG_CONFIG_MAX_BYTES  3500
#define CAN_LOG_MAX_IDS           64
#define CAN_LOG_READ_MAX_ENTRIES  128

// 单条录制状态（用于网页显示）
typedef struct {
    bool     recording;   // 是否正在录制
    uint32_t count;       // 已录制的条数
    uint32_t capacity;    // 缓冲总容量（条）
    uint32_t dropped;     // 已知的记录器丢弃条数（录满立即停止，正常为 0）
    uint32_t duration_ms; // 录制持续时长（从开始到停止）
    bool     psram_ok;    // PSRAM 缓冲是否分配成功
    char     session[17]; // 启动随机值 + 录制代次，固定 16 位十六进制
    bool     full;        // 容量已满，自动停止且保留原始帧
    bool     protected_record; // 必须显式释放才允许覆盖本次记录
    uint32_t rx_lost;     // 本次录制期间 TWAI 丢失/溢出计数之和
    bool     rx_quality_known; // 是否成功取得 TWAI 起止计数
} can_log_status_t;

// 初始化：从 PSRAM 分配记录缓冲（失败不致命，录制功能禁用）
esp_err_t can_log_init(void);

// 开始录制（清空已有数据，重新计时）
esp_err_t can_log_start(void);

// 冻结本次信号定义和去重后的 CAN ID。失败不会改变已有记录。
esp_err_t can_log_start_config(const char *signals_json, const uint32_t *ids,
                               size_t id_count, can_log_status_t *out);

// 停止录制（保留已录数据，可继续导出）
esp_err_t can_log_stop(void);
esp_err_t can_log_stop_session(const char *session, can_log_status_t *out);

// 清空停止且未保护的数据；录制中、导出中或受保护时拒绝操作
esp_err_t can_log_clear(void);

// 查询录制状态
void can_log_get_status(can_log_status_t *out);

// 是否正在录制（供 /api/rec/clear 守卫）
bool can_log_is_recording(void);

// 获取录制缓冲区基址及当前条数（供导出模块直接分块读取）
// 长时间读取前必须 acquire 已停止的会话，读取后 release。
// 录制中 count 仅为动态快照，不允许将裸指针用于稳定导出。
esp_err_t can_log_get_buffer(can_msg_entry_t **base, uint32_t *count);

// 会话检查、状态快照和有界复制在同一把锁内完成。offset 即帧序号（从 0 开始）。
esp_err_t can_log_read(const char *session, uint32_t offset, can_msg_entry_t *out,
                      uint32_t max_entries, uint32_t *out_count,
                      can_log_status_t *status);
// 浏览器补齐期间持有短期可续租保护，最终批次处理成功后显式 ACK。
esp_err_t can_log_read_client(const char *session, const char *client, uint32_t offset,
                             can_msg_entry_t *out, uint32_t max_entries,
                             uint32_t *out_count, can_log_status_t *status);
esp_err_t can_log_sync_done(const char *session, const char *client, uint32_t count);
esp_err_t can_log_copy_config(const char *session, char *out, size_t size);

// 导出时固定已停止的原始记录，释放后仍需显式 unprotect 才能覆盖。
esp_err_t can_log_acquire(const char *session, can_log_status_t *out);
void can_log_release(const char *session);
esp_err_t can_log_unprotect(const char *session);
esp_err_t can_log_unprotect_client(const char *session, const char *client);

// 内部使用：录制模式下写入一条消息（由 can_rx_task 调用）
// 录满自动停止录制（不覆盖不阻塞）
void can_log_write(const can_msg_entry_t *entry);
