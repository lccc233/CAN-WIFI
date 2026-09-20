# ESP32-S3 CAN Bus Monitor

基于 ESP-IDF 的 CAN 总线监控工具：ESP32-S3 通过 TWAI（CAN 2.0）接收总线数据，以 Web 页面实时展示，支持手动发送 CAN 帧，并内置 **PSRAM 历史数据记录仪**（录满即停 + CSV 导出）。

## 功能

- **CAN 总线监控**：TWAI 驱动，**250 kbps**，NORMAL 模式，TX=GPIO5，RX=GPIO4
- **Web 实时界面**（HTTP Server，页面 200ms 轮询刷新）：
  - 顶层页签：**CAN Monitor**（按 ID 分组表格）/ **电压电流曲线**（0x18FF0282 双 Y 轴实时曲线）
  - 按 ID 分组展示最新消息，列：`ID / Count / Freq / DLC / Ext / Data / Last Time`
  - **Freq**：每个 ID 的发送频率（条/秒，基于滚动 1 秒窗口统计）
  - 点击某行进入该 ID 的详情，可切换 **Chart（曲线）/ Table（原始报文）** 两个视图
  - **曲线视图**：把每条报文的前 4 字节转成整数画出随时间变化的曲线，支持字节序（大端/小端）与有符号切换
  - 支持手动发送任意 CAN 帧、一键清空
- **PSRAM 记录仪**：
  - 头部控制条：**Record（开始/停止录制）** / **Export CSV（导出）** / 录制状态（条数/容量/丢弃）
  - 只录制两个报文 ID：`0x18FF0182`（10ms，电机转矩/转速/故障）和 `0x18FF0282`（50ms，母线电流/电压），其余 ID 仅走监控环形缓冲
  - 缓冲在 PSRAM（约 6MB，**约 31 万条，两报文合计 120 帧/秒 ≈ 44 分钟**），**录满自动停止**（不覆盖、不阻塞接收），丢弃计数显示在页面
  - CSV 导出为物理值列：`no,timestamp_ms,id,torque,speed_rpm,fault_code,fault_level,current_A,voltage_V`
- **WiFi SoftAP**：设备自己发布热点 `SDLG-CAN-WIFI`（密码 `12345678`），手机/电脑连上后访问 `http://192.168.4.1` 或 `http://can-monitor.local`
- **状态灯**（WS2812，GPIO48）：**无设备连接 WiFi → 红灯常亮；有设备连接 → 炫彩**（色相循环）

## 硬件

- **主控**：ESP32-S3-DevKitC（模组 N16R8，带 8MB 八线 PSRAM）
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

PSRAM 通过 `sdkconfig.defaults` 启用（OCT 八线 / 80MHz / Flash DIO 80MHz / 16MB），
不要手动改 `sdkconfig`——**手工插入的配置块会被构建系统重写丢弃**（见 HANDOFF）。
烧录走 **UART 模式**：USB 口本身暴露 COM 口（如 COM6），用 esptool 直接烧录，无需 OpenOCD。

> **改了目录名或移动过项目？** 先删掉 `build/` 再构建。CMakeCache 会写死项目绝对路径，
> 沿用旧 `build/` 会报 `CMAKE_C_COMPILER not found`。
>
> **改了网页后烧录了却看不到变化？** 浏览器缓存了旧页面，按 `Ctrl+Shift+R` 强制刷新。
>
> **怀疑改动没生效？** 比对时间戳：`build/can_monitor.bin` 应该新于 `main/` 下的源文件。

## 使用

1. 设备上电后自动发布热点 `SDLG-CAN-WIFI`（密码 `12345678`），手机/电脑连接该热点
   - **注意**：`CONFIG_SPIRAM_MEMTEST=y` 会让上电慢几秒（PSRAM 内存测试），正常
2. 浏览器打开 `http://192.168.4.1` 或 `http://can-monitor.local`
3. 在底部发送区填写 ID / DLC / Data 即可向总线发送 CAN 帧
4. **看曲线**：点主表格里任意一行 → 进入该 ID 详情，默认显示曲线，右上角可切到 `Table` 看原始报文
5. **电压/电流曲线**：顶部页签切到「电压电流曲线」，实时刻画 0x18FF0282 的
   母线电流（左轴，A）与母线电压（右轴，V）；无需先录制
6. **录制/导出**：点 `Record` 开始录制（网页显示 REC 条数/容量/时长），再点停止；
   点 `Export CSV` 下载已录数据（Excel 可直接打开）

### 电压/电流信号定义（来自协议表）

报文 `0x18FF0282`（扩展帧，50ms，8 字节，字节序默认**小端**）：

| 信号 | 位置 | 公式 | 范围 |
|------|------|------|------|
| 母线电流 | byte0-1 | `raw × 0.1 − 1000` (A) | −1000.0 ~ +1000.0 |
| 母线电压 | byte2-3 | `raw × 0.1` (V) | 0 ~ 1000.0 |

- **哨兵值**：电流原始值 `0x2710` 表示“U 相电流零漂故障”，曲线会跳过该点不画（CSV 仍导出原始值）
- **字节序不确定**：曲线幅值明显不对时，把 `main/signal_decode.h` 中
  `SIG_LITTLE_ENDIAN` 改为 `0`（Motorola 大端）重新编译
- 电机报文 `0x18FF0182`（10ms）：输出转矩 = `raw−3000`（byte1-2）、当前转速 = `raw−15000`（byte3-4）、故障代码 = byte6、故障等级 = byte7 低 4 位

### 曲线取值规则（详情视图）

曲线的 Y 值 = 该报文**前 4 个字节**按所选字节序拼成的 32 位整数，例如报文
`01 02 03 04 05 06 07 08`：

| 选项 | 取值 | 说明 |
|------|------|------|
| 大端（默认） | `0x01020304` = 16909060 | 首字节是最高位，CAN 信号的常见约定 |
| 小端 | `0x04030201` = 67305985 | 首字节是最低位 |

- **有符号**勾选后按 `int32` 解释（`FF FF FF FF` = −1，`80 00 00 00` = −2147483648）
- DLC 不足 4 字节时按实际字节数处理（如 `01 02` → 258），不做符号扩展
- 曲线最多绘制**最近 1200 点**，更早的点仍保留在 `Table` 视图和统计中

## Web API

| 接口 | 说明 |
|------|------|
| `GET /api/messages` | 返回 `{rec:{on,cnt,cap,drop,ms,psram}, total, freqs:[{id,f}], messages:[{t,id,dlc,ext,data}], vi:[{t,c,v,f}]}`；`vi` 为 0x18FF0282 解码值（c/v 为物理值×10，f=电流哨兵故障） |
| `POST /api/send` | 发送 CAN 帧（body 含 id/dlc/data/extended） |
| `POST /api/clear` | 清空消息缓冲与频率统计 |
| `POST /api/rec/start` | 开始录制（清空重新计时） |
| `POST /api/rec/stop` | 停止录制（保留数据） |
| `POST /api/rec/clear` | 清空录制缓冲 |
| `GET /api/export` | CSV 流式下载全部录制数据（物理值列） |

## 目录结构

```
main/
├── main.c             # 初始化：NVS → WiFi → CAN → 记录器 → Web → LED
├── can.c / can.h      # TWAI 驱动、RX 任务、每 ID 频率统计
├── can_logger.c/.h    # PSRAM 录制缓冲（双 ID 过滤，录满即停）
├── signal_decode.c/.h # 0x18FF0182/0x18FF0282 信号解码 + 实时 V/I 环形缓冲
├── wifi.c / wifi.h    # WiFi SoftAP + mDNS + 客户端计数
├── led.c / led.h      # WS2812 状态灯（红=无客户端，炫彩=有客户端）
├── web_server.c       # HTTP 服务与 JSON API / CSV 导出
└── web_page.h         # 前端页面（内嵌 HTML/CSS/JS：监控表 + 电压电流双 Y 轴曲线）

sdkconfig.defaults # kconfig 默认值（PSRAM/Flash），改配置改这里，不要手改 sdkconfig
```

