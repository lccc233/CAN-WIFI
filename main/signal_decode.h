#pragma once

#include <stdint.h>
#include <stdbool.h>

// 目标报文 ID（扩展帧）
#define SIG_ID_MOTOR_DRIVE  0x18FF0182UL  // 10ms：输出转矩/当前转速/故障代码/故障等级
#define SIG_ID_BUS_VI       0x18FF0282UL  // 50ms：母线电流/母线电压

// 16 位原始值字节序：1=Intel 小端（byte0=低字节），0=Motorola 大端。
// 协议表未标注，默认小端，实测曲线方向/幅值不对时切为 0
#define SIG_LITTLE_ENDIAN   1

// ---- 电机驱动报文 0x18FF0182 ----
typedef struct {
    int16_t  torque;       // 输出转矩 raw-3000
    int16_t  speed_rpm;    // 当前转速 raw-15000
    uint8_t  fault_code;   // 故障代码
    uint8_t  fault_level;  // 故障等级 0~15
    bool     valid;        // 数据长度是否足够
} sig_motor_t;

// ---- 母线电压电流报文 0x18FF0282 ----
typedef struct {
    bool     current_fault; // 0x2710 哨兵：U相电流零漂故障
    int16_t  current_x10;   // 母线电流 A*10，范围 -10000~+10000
    uint16_t voltage_x10;   // 母线电压 V*10，范围 0~10000
    bool     valid;
} sig_bus_vi_t;

// 曲线显示用采样点（时间戳 + 解码值）
typedef struct {
    uint32_t t;          // 时间戳 ms
    int16_t  current_x10;
    uint16_t voltage_x10;
    uint8_t  fault;      // 电流哨兵故障标志
} sig_vi_point_t;

// 解析电机驱动报文
void sig_decode_motor(const uint8_t *data, uint8_t dlc, sig_motor_t *out);

// 解析母线电压电流报文
void sig_decode_bus_vi(const uint8_t *data, uint8_t dlc, sig_bus_vi_t *out);

// 判断 ID 是否为录制目标
bool sig_is_record_id(uint32_t id);

// 实时 V/I 显示环形缓冲（曲线页数据源，不受录制开关影响）
void sig_vi_push(uint32_t timestamp_ms, const sig_bus_vi_t *vi);
// 当前缓冲内点数
int  sig_vi_count(void);
// 拷贝最近 max 条采样点（按时间升序），返回实际条数
int  sig_vi_snapshot(sig_vi_point_t *out, int max);
// 读取倒数第 idx 条（idx=0 为最新一条），失败返回 false
// 线程安全、无共享缓冲，可被多个并发请求逐点读取
bool sig_vi_get_back(int idx, sig_vi_point_t *out);
