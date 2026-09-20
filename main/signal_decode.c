#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "signal_decode.h"

// 读取 16 位原始值（支持小端/大端切换）
static uint16_t sig_rd16(const uint8_t *data)
{
#if SIG_LITTLE_ENDIAN
    return (uint16_t)(data[0] | (data[1] << 8));
#else
    return (uint16_t)((data[0] << 8) | data[1]);
#endif
}

void sig_decode_motor(const uint8_t *data, uint8_t dlc, sig_motor_t *out)
{
    assert(dlc <= 8);  // TWAI 驱动保证，防回归
    memset(out, 0, sizeof(*out));
    if (dlc < 8) return;  // 故障等级在 byte7，长度不足整体无效

    uint16_t t_raw = sig_rd16(data + 1);   // byte1..2: raw-3000
    uint16_t s_raw = sig_rd16(data + 3);   // byte3..4: raw-15000
    out->torque      = (int16_t)t_raw - 3000;
    out->speed_rpm   = (int16_t)s_raw - 15000;
    out->fault_code  = data[6];
    out->fault_level = data[7] & 0x0F;
    out->valid = true;
}

void sig_decode_bus_vi(const uint8_t *data, uint8_t dlc, sig_bus_vi_t *out)
{
    assert(dlc <= 8);  // TWAI 驱动保证，防回归
    memset(out, 0, sizeof(*out));
    if (dlc < 4) return;

    uint16_t i_raw = sig_rd16(data);       // byte0..1
    uint16_t v_raw = sig_rd16(data + 2);   // byte2..3

    out->current_fault = (i_raw == 0x2710);  // U相电流零漂故障哨兵值
    // raw*0.1-1000 (A)：先按 int32 运算再钳位，防原始值越界时 int16 溢出
    int32_t c = (int32_t)i_raw - 10000;
    if (c > 32767) c = 32767;
    if (c < -32768) c = -32768;
    out->current_x10   = (int16_t)c;
    out->voltage_x10   = v_raw;                   // raw*0.1 (V)
    out->valid = true;
}

bool sig_is_record_id(uint32_t id)
{
    return id == SIG_ID_MOTOR_DRIVE || id == SIG_ID_BUS_VI;
}

// ---- 实时 V/I + 电机参数显示环形缓冲 ----
#define VI_RING_SIZE  1024  // 必须为 2 的幂（用 & 掩码取模），50ms 周期下约 51 秒

// 最新一帧电机参数（10ms 报文高频更新，50ms 曲线点采样时读取）
static sig_motor_t s_motor_cache;
static SemaphoreHandle_t s_motor_mutex;

void sig_motor_set(uint32_t timestamp_ms, const sig_motor_t *motor)
{
    (void)timestamp_ms;
    if (!motor || !motor->valid) return;
    if (!s_motor_mutex) {
        s_motor_mutex = xSemaphoreCreateMutex();
        if (!s_motor_mutex) return;
    }
    xSemaphoreTake(s_motor_mutex, portMAX_DELAY);
    s_motor_cache = *motor;
    xSemaphoreGive(s_motor_mutex);
}

void sig_motor_get(sig_motor_t *out)
{
    if (!s_motor_mutex || !out) return;
    xSemaphoreTake(s_motor_mutex, portMAX_DELAY);
    *out = s_motor_cache;
    xSemaphoreGive(s_motor_mutex);
}

typedef struct {
    sig_vi_point_t buf[VI_RING_SIZE];
    SemaphoreHandle_t mutex;
    uint32_t head;
    uint32_t count;
} vi_ring_t;

static vi_ring_t s_vi_ring;

void sig_vi_push(uint32_t timestamp_ms, const sig_bus_vi_t *vi, const sig_motor_t *motor)
{
    if (!s_vi_ring.mutex) {
        s_vi_ring.mutex = xSemaphoreCreateMutex();
        if (!s_vi_ring.mutex) return;
    }
    xSemaphoreTake(s_vi_ring.mutex, portMAX_DELAY);
    sig_vi_point_t *p = &s_vi_ring.buf[s_vi_ring.head & (VI_RING_SIZE - 1)];
    p->t = timestamp_ms;
    p->current_x10 = vi->current_x10;
    p->voltage_x10 = vi->voltage_x10;
    p->fault = vi->current_fault ? 1 : 0;
    if (motor && motor->valid) {
        p->torque = motor->torque;
        p->rpm = motor->speed_rpm;
        p->motor_v = 1;
    } else {
        p->torque = 0;
        p->rpm = 0;
        p->motor_v = 0;
    }
    s_vi_ring.head++;
    if (s_vi_ring.count < VI_RING_SIZE) s_vi_ring.count++;
    xSemaphoreGive(s_vi_ring.mutex);
}

int sig_vi_count(void)
{
    if (!s_vi_ring.mutex) return 0;
    xSemaphoreTake(s_vi_ring.mutex, portMAX_DELAY);
    int n = (int)s_vi_ring.count;
    xSemaphoreGive(s_vi_ring.mutex);
    return n;
}

bool sig_vi_get_back(int idx, sig_vi_point_t *out)
{
    if (!s_vi_ring.mutex || idx < 0) return false;
    bool ok = false;
    xSemaphoreTake(s_vi_ring.mutex, portMAX_DELAY);
    if ((uint32_t)idx < s_vi_ring.count) {
        uint32_t pos = (s_vi_ring.head - 1 - (uint32_t)idx) & (VI_RING_SIZE - 1);
        *out = s_vi_ring.buf[pos];
        ok = true;
    }
    xSemaphoreGive(s_vi_ring.mutex);
    return ok;
}

int sig_vi_snapshot(sig_vi_point_t *out, int max)
{
    if (!s_vi_ring.mutex) return 0;
    xSemaphoreTake(s_vi_ring.mutex, portMAX_DELAY);
    int n = (s_vi_ring.count < (uint32_t)max) ? (int)s_vi_ring.count : max;
    uint32_t start = (s_vi_ring.head - (uint32_t)n) & (VI_RING_SIZE - 1);
    for (int i = 0; i < n; i++) {
        out[i] = s_vi_ring.buf[(start + (uint32_t)i) & (VI_RING_SIZE - 1)];
    }
    xSemaphoreGive(s_vi_ring.mutex);
    return n;
}
