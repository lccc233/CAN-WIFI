#pragma once

#include "esp_err.h"

// 初始化 WS2812 状态灯任务：
//   无 WiFi 客户端连接 → 红灯常亮
//   有 WiFi 客户端连接 → 炫彩（色相循环）
esp_err_t led_init(void);
