#pragma once

#include "esp_err.h"
#include <stdbool.h>

// SoftAP 热点配置（设备自己发布 WiFi，IP 固定 192.168.4.1）
#define WIFI_SSID        "SDLG-CAN-WIFI"
#define WIFI_AP_PASS     "12345678"   // 密码留空 "" 则为开放网络
#define WIFI_AP_CHANNEL  1
#define WIFI_AP_MAX_CONN 4
#define MDNS_HOSTNAME    "can-monitor"

esp_err_t wifi_init_softap(void);
bool      wifi_is_connected(void);
int       wifi_get_sta_count(void);   // 当前连接的客户端数
