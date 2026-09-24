#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---- 默认配置（NVS 中无保存值时使用；可经串口命令修改并存入 NVS）----
#define WIFI_DEFAULT_STA_SSID   "ABCDEF"
#define WIFI_DEFAULT_STA_PASS   "A12345678"

// AP（热点）模式默认：实际 SSID 为 CAN-Monitor-XXXX（XXXX=MAC 后 4 位）
#define WIFI_DEFAULT_AP_PASS    "12345678"

#define MDNS_HOSTNAME    "can-monitor"

// 运行模式（枚举值与 NVS 存储一致，勿改）
typedef enum {
    WIFI_CFG_MODE_STA = 0,   // 连接现有路由器
    WIFI_CFG_MODE_AP  = 1,   // 设备自建热点
} wifi_cfg_mode_t;

// IP 分配方式
typedef enum {
    WIFI_IP_AUTO_250 = 0,    // DHCP 拿到 IP 后自动改绑为同网段 xxx.xxx.xxx.250（默认）
    WIFI_IP_STATIC   = 1,    // 使用串口指定的固定 IP
} wifi_ip_mode_t;

// 初始化：读取 NVS 配置并按模式启动 STA 或 AP
esp_err_t wifi_init(void);
// STA：已连上 AP 且拿到 IP；AP：热点已启动（LED 据此显示状态）
bool      wifi_is_connected(void);

// ---- 串口配置接口（写入 NVS + 尽量立即生效）----

// 切换运行模式，保存后需重启生效
esp_err_t wifi_cfg_set_mode(wifi_cfg_mode_t mode);

// 设置 STA 的 SSID/密码（SSID 1~32 字节，密码 8~63 字符），保存后自动重连
esp_err_t wifi_cfg_set_sta_credentials(const char *ssid, const char *pass);

// 设置 IP 分配方式；STATIC 时 static_ip 必须为合法 IPv4（如 "192.168.1.250"）
// STA 在线时断开重连，按新规则重新绑定
esp_err_t wifi_cfg_set_ip_mode(wifi_ip_mode_t mode, const char *static_ip);

wifi_cfg_mode_t wifi_cfg_get_mode(void);
wifi_ip_mode_t  wifi_cfg_get_ip_mode(void);
void      wifi_cfg_get_sta_ssid(char *out, size_t len);
void      wifi_cfg_get_sta_pass(char *out, size_t len);
void      wifi_cfg_get_static_ip(char *out, size_t len);
void      wifi_cfg_get_ap_ssid(char *out, size_t len);   // CAN-Monitor-XXXX

// ---- 状态查询（供串口 info 命令等）----
typedef struct {
    wifi_cfg_mode_t mode;
    bool link_up;            // STA 已拿 IP / AP 已启动
    char ssid[33];           // STA 模式为目标 SSID，AP 模式为本机热点名
    int  rssi;               // STA 模式信号强度，未连接为 0
    uint8_t channel;
    int  ap_clients;         // AP 模式已连接的设备数
    char ip[16];
    char netmask[16];
    char gw[16];
    uint8_t mac[6];          // 当前模式对应接口的 MAC
} wifi_status_t;
void wifi_get_status(wifi_status_t *out);
