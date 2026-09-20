#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "time_sync.h"

typedef struct {
    bool     synced;
    int64_t  epoch_base;    // 校准时刻的真实 Unix 毫秒（UTC）
    uint32_t boot_base;     // 校准时刻的开机毫秒
    int      tz_min;        // 浏览器时区偏移（东区为负）
    SemaphoreHandle_t mutex;
} time_sync_t;

static time_sync_t s_ts;

static void ts_init(void)
{
    if (!s_ts.mutex) s_ts.mutex = xSemaphoreCreateMutex();
}

static uint32_t boot_ms_now(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void time_sync_set(int64_t epoch_ms, int tz_min)
{
    ts_init();
    xSemaphoreTake(s_ts.mutex, portMAX_DELAY);
    s_ts.epoch_base = epoch_ms;
    s_ts.boot_base = boot_ms_now();
    s_ts.tz_min = tz_min;
    s_ts.synced = true;
    xSemaphoreGive(s_ts.mutex);
}

bool time_sync_get(uint32_t boot_ms, int64_t *out_epoch_ms)
{
    ts_init();
    bool ok = false;
    xSemaphoreTake(s_ts.mutex, portMAX_DELAY);
    if (s_ts.synced) {
        int64_t elapsed = (int64_t)boot_ms - (int64_t)s_ts.boot_base;
        // 帧时间戳早于校准时刻（校准前的历史帧）：回退到 0，避免负数时间
        if (elapsed < 0) elapsed = 0;
        *out_epoch_ms = s_ts.epoch_base + elapsed;
        ok = true;
    }
    xSemaphoreGive(s_ts.mutex);
    return ok;
}

bool time_sync_is_synced(void)
{
    ts_init();
    xSemaphoreTake(s_ts.mutex, portMAX_DELAY);
    bool s = s_ts.synced;
    xSemaphoreGive(s_ts.mutex);
    return s;
}

bool time_sync_format_boot_ms(uint32_t boot_ms, char *out, size_t out_len)
{
    int64_t epoch;
    if (!time_sync_get(boot_ms, &epoch)) return false;

    // 浏览器本地时间 = UTC epoch - getTimezoneOffset 分钟（东区为负）
    int64_t sec = epoch / 1000 - (int64_t)s_ts.tz_min * 60;
    int msec = (int)(epoch % 1000);
    if (msec < 0) { msec += 1000; sec -= 1; }

    // 简单公历换算（1970 起，够用到 2100 年）
    int64_t days = sec / 86400;
    int64_t rem = sec % 86400;
    int hh = (int)(rem / 3600), mm = (int)(rem % 3600 / 60), ss = (int)(rem % 60);

    // 民用历日：从 1970-01-01 往后推
    static const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int year = 1970;
    for (;;) {
        int64_t ylen = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) ? 366 : 365;
        if (days >= ylen) { days -= ylen; year++; } else break;
    }
    int mon = 0;
    for (;;) {
        int mlen = mdays[mon];
        if (mon == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) mlen = 29;
        if (days >= mlen) { days -= mlen; mon++; } else break;
    }
    int day = (int)days + 1;

    snprintf(out, out_len, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             year, mon + 1, day, hh, mm, ss, msec);
    return true;
}
