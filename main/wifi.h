#pragma once

#include "esp_err.h"
#include <stdbool.h>

// STA 模式配置（连接现有 WiFi 路由器，IP 由路由器 DHCP 分配；
// 启动日志会打印获得的 IP，也可用 mDNS 访问 http://can-monitor.local）
#define WIFI_STA_SSID    "lc"
#define WIFI_STA_PASS    "12345678"
#define MDNS_HOSTNAME    "can-monitor"

esp_err_t wifi_init_sta(void);
bool      wifi_is_connected(void);   // 已连上 AP 且拿到 IP
