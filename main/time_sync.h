#pragma once

#include <stdint.h>
#include <stdbool.h>

// 浏览器授时（纯内存，重启后失效需重新授时）
// 真实时刻 = epoch_base + (帧时间戳 boot_ms - boot_base)

// 由 /api/time 调用：epoch_ms 为浏览器 Unix 毫秒（UTC 语义），
// tz_min 为浏览器 new Date().getTimezoneOffset()（东区为负，如中国 -480）
void time_sync_set(int64_t epoch_ms, int tz_min);

// 查询换算基准与同步状态
bool time_sync_get(uint32_t boot_ms, int64_t *out_epoch_ms);

// 是否已授时
bool time_sync_is_synced(void);

// 把帧时间戳(开机相对ms)格式化为 "2026-09-20 14:35:01.123"
// 未授时时返回 false（out 不变）
bool time_sync_format_boot_ms(uint32_t boot_ms, char *out, size_t out_len);
