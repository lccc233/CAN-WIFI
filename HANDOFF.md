# 交接文档：ESP32-S3 CAN Bus Monitor

## 项目信息
- **路径**: `C:\Users\liche\esp32s3\led_strip_rmt_ws2812`
- **主文件**: `main/main.c`, `main/can.c`, `main/wifi.c`, `main/web_server.c`
- **ESP-IDF 版本**: 5.3.1，路径 `C:\Users\liche\esp\v5.3.1\esp-idf`
- **开发板**: ESP32-S3-DevKitC
- **CAN 收发器**: SIT1042AQT/3（STB 接地，VCC 5V，**VIO 接 3.3V**）

## 项目功能
1. **WiFi SoftAP** (设备自己发布热点 SDLG-CAN-WIFI，密码 12345678)
2. **CAN 总线监控** (TWAI 驱动, 250kbps, NORMAL 模式)
3. **网页 CAN 工具** (HTTP Server，暗色主题表格显示收发 CAN 消息)
4. **状态灯** (WS2812 GPIO48：无客户端连接=红灯常亮，有客户端=炫彩)

## CAN 不通根因 (2026-07-29)

### 根本原因
SIT1042 CAN 收发器模块的 **TX/RX 默认电平为 5V**，而 ESP32-S3 引脚为 **3.3V**。电平不匹配导致通信失败。

### 解决方案
将 SIT1042 模块的 **VIO 引脚接入 3.3V**，使 TX/RX 工作在 3.3V 电平，与 ESP32-S3 兼容。

### 现象回顾
- 之前 TX=RX 同一引脚回环测试成功，但实际 CAN 通信失败
- 不同 GPIO 组合 (GPIO5+GPIO4, GPIO1+GPIO2 等) 均失败
- 根因不是 GPIO 矩阵延迟问题，而是电平不匹配

---

## 当前调试状态

### 已完成的测试

1. **GPIO 直接读写测试** — GPIO4 读 GPIO5 正常
2. **GPIO 矩阵内部回环测试** — TX=RX 同一 GPIO，`.self = 1`，成功
3. **外部物理引脚 TX/RX 分离测试** — 之前失败（电平问题，已解决）

### 已验证的 GPIO 组合
- GPIO5(TX) + GPIO4(RX) — 当前使用，VIO 接 3.3V 后正常

## 当前代码状态
- 多文件架构：`main.c`, `wifi.c`, `can.c`, `web_server.c`, `web_page.h`
- TWAI 配置：`TWAI_MODE_NORMAL`，250kbps，TX=GPIO5，RX=GPIO4
- WiFi SoftAP 模式发布热点 SDLG-CAN-WIFI（密码 12345678），IP 192.168.4.1，mDNS: `can-monitor.local`
- 网页 200ms 轮询显示 CAN 消息，支持手动发送

## 关键代码位置
| 文件 | 说明 |
|------|------|
| `main/can.c` | TWAI 驱动初始化、RX 任务、告警处理、发送 API |
| `main/wifi.c` | WiFi SoftAP 发布、客户端计数、mDNS |
| `main/led.c` | WS2812 状态灯（无客户端=红，有客户端=炫彩） |
| `main/web_server.c` | HTTP 路由：/ /api/messages /api/send /api/clear |
| `main/web_page.h` | 嵌入式 HTML/CSS/JS 网页 |
| `main/main.c` | 入口：NVS → WiFi → CAN → HTTP Server → LED |
