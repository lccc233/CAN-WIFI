# ESP32-S3 CAN Bus Monitor

基于 ESP-IDF 的 CAN 总线监控工具：ESP32-S3 通过 TWAI（CAN 2.0）接收总线数据，以 Web 页面实时展示，支持手动发送 CAN 帧。

## 功能

- **CAN 总线监控**：TWAI 驱动，**250 kbps**，NORMAL 模式，TX=GPIO5，RX=GPIO4
- **Web 实时界面**（HTTP Server，页面 200ms 轮询刷新）：
  - 按 ID 分组展示最新消息，列：`ID / Count / Freq / DLC / Ext / Data / Last Time`
  - **Freq**：每个 ID 的发送频率（条/秒，基于滚动 1 秒窗口统计）
  - 点击某行可查看该 ID 的历史消息详情
  - 支持手动发送任意 CAN 帧、一键清空
- **WiFi SoftAP**：设备自己发布热点 `SDLG-CAN-WIFI`（密码 `12345678`），手机/电脑连上后访问 `http://192.168.4.1` 或 `http://can-monitor.local`
- **状态灯**（WS2812，GPIO48）：**无设备连接 WiFi → 红灯常亮；有设备连接 → 炫彩**（色相循环）

## 硬件

- **主控**：ESP32-S3-DevKitC
- **CAN 收发器**：SIT1042AQT/3
  - STB 接地
  - VCC 接 5V
  - **VIO 必须接 3.3V**（TX/RX 电平与 ESP32-S3 匹配）
- **接线**：
  | 功能 | GPIO |
  |------|------|
  | CAN TX | GPIO5 |
  | CAN RX | GPIO4 |
  | WS2812 DIN | GPIO48 |
- **供电/烧录**：USB 线接板载 USB-Serial/JTAG 口

## 构建与烧录

```bash
idf.py set-target esp32s3
idf.py -p <COM口> build flash monitor
```

烧录走 **UART 模式**：USB 口本身暴露 COM 口（如 COM5），用 esptool 直接烧录，无需 OpenOCD。
（如需 JTAG 调试，将 `idf.flashType` 改为 `JTAG`，OpenOCD 接口使用板载 USB-JTAG 的 `interface/esp_usb_jtag.cfg`。）

## 使用

1. 设备上电后自动发布热点 `SDLG-CAN-WIFI`（密码 `12345678`），手机/电脑连接该热点
2. 浏览器打开 `http://192.168.4.1` 或 `http://can-monitor.local`
3. 在底部发送区填写 ID / DLC / Data 即可向总线发送 CAN 帧

## Web API

| 接口 | 说明 |
|------|------|
| `GET /api/messages` | 返回 `{total, freqs:[{id,f}], messages:[{t,id,dlc,ext,data}]}` |
| `POST /api/send` | 发送 CAN 帧（body 含 id/dlc/data/extended） |
| `POST /api/clear` | 清空消息缓冲与频率统计 |

## 目录结构

```
main/
├── main.c          # 初始化：NVS → WiFi → CAN → Web → LED
├── can.c / can.h   # TWAI 驱动、RX 任务、每 ID 频率统计
├── wifi.c / wifi.h # WiFi SoftAP + mDNS + 客户端计数
├── led.c / led.h   # WS2812 状态灯（红=无客户端，炫彩=有客户端）
├── web_server.c    # HTTP 服务与 JSON API
└── web_page.h      # 前端页面（内嵌 HTML/CSS/JS）
```
