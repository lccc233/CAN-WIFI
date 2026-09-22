#pragma once

#include "esp_err.h"
#include <stdbool.h>

// STA 模式配置（连接现有 WiFi 路由器；mDNS: http://can-monitor.local）
#define WIFI_STA_SSID    "lc"
#define WIFI_STA_PASS    "12345678"

// ---- 固定 IP（静态地址，网页地址永远不变）----
// 1 = 使用下面的静态 IP；0 = 路由器 DHCP 自动分配（IP 看启动日志/路由器后台）
// 注意：IP 必须与路由器同网段、且在 DHCP 自动分配池之外（一般选 .2xx 段）。
// 网段配错设备将不可达——把开关改回 0 重烧，或改成正确网段。
#define WIFI_STA_STATIC_IP   1
#define WIFI_STA_IP          "10.31.134.250"
#define WIFI_STA_GATEWAY     "10.31.134.224"
#define WIFI_STA_NETMASK     "255.255.255.0"

#define MDNS_HOSTNAME    "can-monitor"

esp_err_t wifi_init_sta(void);
bool      wifi_is_connected(void);   // 已连上 AP 且拿到 IP
