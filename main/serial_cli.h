#pragma once

#include "esp_err.h"

// 串口配置台：UART0 与 USB-Serial/JTAG 两个口均可输入命令
// 命令：help / info / mode ap|sta / ssid <名称> / pass <密码> / ip [auto|x.x.x.x] / reboot
esp_err_t serial_cli_init(void);
