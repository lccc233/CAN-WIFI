#include <string.h>
#include <assert.h>
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
