# ESP32-S3 CAN Bus Monitor

基于 ESP-IDF 的 CAN 总线监控工具：ESP32-S3 通过 TWAI（CAN 2.0）接收总线数据，以 Web 页面实时展示，支持手动发送 CAN 帧，并内置 **PSRAM 历史数据记录仪**（录满即停 + CSV 导出）。

## 功能

- **CAN 总线监控**：TWAI 驱动，**250 kbps**，NORMAL 模式，TX=GPIO5，RX=GPIO4
- **Web 实时界面**（HTTP Server，页面 200ms 轮询刷新）：
  - 顶层页签：**CAN Monitor**（按 ID 分组表格）/ **电压电流曲线**（固定 20s 双画布）/ **自定义曲线**（任意 ID 自定义信号）
  - 按 ID 分组展示最新消息（**按 ID 从小到大排序**），列：`ID / Count / Freq / DLC / Ext / Data / Last Time`
  - **Freq**：每个 ID 的发送频率（条/秒，基于滚动 1 秒窗口统计）
  - 点击某行进入该 ID 的**原始报文历史表**（自动滚动跟随最新），无内部曲线
  - 发送面板固定在 CAN Monitor 页面底部；进入详情页时隐藏（替换为返回按钮）
  - 支持手动发送任意 CAN 帧、一键清空
- **信号曲线页**（无需先录制，200ms 轮询重绘）：
  - 上画布：母线电流 **I(A，左轴)** + 母线电压 **U(V，右轴)**（0x18FF0282）
  - 下画布：输出转矩 **T(Nm，左轴)** + 当前转速 **n(rpm，右轴)**（0x18FF0182，50ms 采样并入同时间基）
  - 横轴为**固定 20s 滚动窗**（`-20s ~ 0s`，曲线右滑入）；纵轴动态范围加 10% 余量并设最小跨度，哨兵故障点不参与绘制
- **自定义曲线页**（固件零改动，全部在浏览器实现）：
  - 在详情页用「+ 添加曲线」为**任意 CAN ID** 定义信号：起始位/位长/字节序（DBC 位号，Motorola=MSB 位号、Intel=LSB 位号）/符号/factor/offset/单位/颜色，`值 = raw × factor + offset`
  - 每个信号一条独立曲线带（自动量程 + niceStep 刻度），窗口 10s/30s/60s 可切换，可暂停冻结读数
  - Monitor 主表与详情表中，已配置信号覆盖的**字节按信号颜色高亮**，实时数值随轮询刷新
  - **曲线数据记录器**：Record 记录已启用信号的解码值（每信号上限 20 万点 ≈ 1 小时 @50Hz），
    「导出记录CSV」下载宽表（`no,time_rel_ms,信号(单位)@ID,...`），Excel/Python 可直接离线分析
  - 信号配置存 **localStorage**（刷新/断电不丢），「导出配置」可备份为 JSON 迷你 DBC
  - 限制：浏览器只积累**打开页面之后**的数据（200ms 轮询快照去重）；长期历史用导出 CSV 分析
- **浏览器授时**：打开页面自动 `POST /api/time` 校准，CSV 导出的 `time` 列为真实时间
  （未授时时回退为开机相对时间 `boot + HH:MM:SS.mmm`）
- **PSRAM 记录仪**：
  - 头部控制条：**Record（开始/停止录制）** / **Export CSV（导出）** / 录制状态（条数/容量/丢弃）
  - 只录制两个报文 ID：`0x18FF0182`（10ms，电机转矩/转速/故障）和 `0x18FF0282`（50ms，母线电流/电压），其余 ID 仅走监控环形缓冲
  - 缓冲在 PSRAM（约 6MB，**约 37 万条，两报文合计 120 帧/秒 ≈ 52 分钟**），**录满自动停止**（不覆盖、不阻塞接收），丢弃计数显示在页面
  - CSV 导出为物理值列：`no,time,id,torque,speed_rpm,fault_code,fault_level,current_A,voltage_V`
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
烧录走 **UART 模式**：USB 口本身暴露 COM 口，端口号以设备管理器/`idf.py` 枚举为准
（历史配置为 COM6，拔插可能变化），用 esptool 直接烧录，无需 OpenOCD。

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
3. 在底部发送区填写 ID / DLC / Data 即可向总线发送 CAN 帧（该发送面板只在 CAN Monitor 页显示）
4. **看报表**：点主表格里任意一行 → 该 ID 的原始报文历史表，左上 Back to List 返回
5. **信号曲线**：顶部页签切到「电压电流曲线」，上画布
   电流(左轴 A)/电压(右轴 V)，下画布 转矩(左轴 Nm)/转速(右轴 rpm)；无需先录制
6. **自定义曲线**：顶部页签切到「自定义曲线」→ 点某 ID 行进详情 → 「+ 添加曲线」
   定义信号（Motorola 起始位=MSB 位号 byte0 整字节=7；Intel=LSB 位号 byte0=0）；
   回「自定义曲线」页看每个信号一条曲线带，可 Record 记录解码值并导出 CSV/配置
7. **录制/导出**：点 `Record` 开始录制（网页显示 REC 条数/容量/时长），再点停止；
   点 `Export CSV` 下载已录数据（Excel 可直接打开）

### 信号定义（来自协议表）

两个报文（扩展帧，8 字节，16 位原始值字节序默认**小端**）：

**`0x18FF0182`（10ms，电机）**

| 信号 | 位置 | 公式 | 范围 |
|------|------|------|------|
| 输出转矩 | byte1-2 | `raw − 3000` (Nm) | −3000 ~ +3000 |
| 当前转速 | byte3-4 | `raw − 15000` (rpm) | −15000 ~ +15000 |
| 故障代码 | byte6 | 原始值 | 0 ~ 255 |
| 故障等级 | byte7 低 4 位 | 原始值 | 0 ~ 15 |

**`0x18FF0282`（50ms）**

| 信号 | 位置 | 公式 | 范围 |
|------|------|------|------|
| 母线电流 | byte0-1 | `raw × 0.1 − 1000` (A) | −1000.0 ~ +1000.0 |
| 母线电压 | byte2-3 | `raw × 0.1` (V) | 0 ~ 1000.0 |

- **哨兵值**：电流原始值 `0x2710` 表示“U 相电流零漂故障”，曲线会跳过该点不画（CSV 仍导出原始值）
- **字节序不确定**：曲线幅值方向明显不对时，把 `main/signal_decode.h` 中
  `SIG_LITTLE_ENDIAN` 改为 `0`（Motorola 大端）重新编译
- 曲线环长 1024 点（50ms/点 ≈ 51s），日志页显示的是其中最近 400 点 ≈ 20s 窗

## Web API

| 接口 | 说明 |
|------|------|
| `GET /api/messages` | 返回 `{clk:{sync,boot,ep}, rec:{on,cnt,cap,drop,ms,psram}, total, freqs:[{id,f}], messages:[{t,id,dlc,ext,data}], vi:[{t,c,v,f,q,r,m}]}`；`vi` c/v 为物理值×10、q/r 为转矩(Nm)/转速(rpm)、m=电机值有效标志；服务端忙闸期间秒回 `{"busy":1}` |
| `POST /api/time` | 浏览器授时：body `{epoch_ms, tz}`（UTC 毫秒 + 时区偏移分钟） |
| `POST /api/send` | 发送 CAN 帧（body 含 id/dlc/data/extended） |
| `POST /api/clear` | 清空消息缓冲与频率统计 |
| `POST /api/rec/start` | 开始录制（清空重新计时） |
| `POST /api/rec/stop` | 停止录制（保留数据） |
| `POST /api/rec/clear` | 清空录制缓冲 |
| `GET /api/export` | CSV 流式下载全部录制数据（物理值列，time 列已授时为真实时间） |

## 目录结构

```
main/
├── main.c             # 初始化：NVS → WiFi → CAN → 记录器 → Web → LED
├── can.c / can.h      # TWAI 驱动、RX 任务、每 ID 频率统计
├── can_logger.c/.h    # PSRAM 录制缓冲（双 ID 过滤，录满即停）
├── signal_decode.c/.h # 0x18FF0182/0x18FF0282 信号解码 + 实时曲线环形缓冲（I/U/T/n 同时间基）
├── wifi.c / wifi.h    # WiFi SoftAP + mDNS + 客户端计数
├── time_sync.c/.h     # 浏览器授时换算（真实时间戳）
├── led.c / led.h      # WS2812 状态灯（红=无客户端，炫彩=有客户端）
├── web_server.c       # HTTP 服务与 JSON API / CSV 导出
├── web_page.h         # 页面组装宏（拼接 web_head/web_body/web_js 三段源文件）
└── web_head.h web_body.h web_js.h   # 前端页面源（HTML 头+样式 / DOM / JS）

sdkconfig.defaults # kconfig 默认值（PSRAM/Flash），改配置改这里，不要手改 sdkconfig
```

