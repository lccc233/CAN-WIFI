#pragma once

#include "esp_err.h"
#include <stdbool.h>

// STA 模式配置（连接现有 WiFi 路由器；mDNS: http://can-monitor.local）
#define WIFI_STA_SSID    "ABCDEF"
#define WIFI_STA_PASS    "A12345678"

// ---- 固定 IP（当前启用：手机热点网段）----
// 0 = 由热点/路由器 DHCP 自动分配（IP 看启动串口日志 "Got IP:"）
// 1 = 使用下面的静态 IP（网页地址固定不变；需与热点同网段且避开 DHCP 池，
//     网段配错设备不可达——改回 0 重烧即可恢复）
#define WIFI_STA_STATIC_IP   1
#define WIFI_STA_IP          "192.168.43.250"
#define WIFI_STA_GATEWAY     "192.168.43.1"
#define WIFI_STA_NETMASK     "255.255.255.0"

#define MDNS_HOSTNAME    "can-monitor"

esp_err_t wifi_init_sta(void);
bool      wifi_is_connected(void);   // 已连上 AP 且拿到 IP
