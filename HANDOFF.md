# 交接文档：ESP32-S3 CAN Bus Monitor

## 项目信息
- **路径**: `C:\Users\liche\esp32s3\CAN-WIFI`（原名 `led_strip_rmt_ws2812`）
- **主文件**: `main/main.c`, `main/can.c`, `main/can_logger.c`, `main/signal_decode.c`, `main/wifi.c`, `main/web_server.c`, `main/web_page.h`, `main/led.c`
- **ESP-IDF 版本**: 5.3.1，路径 `C:\Users\liche\esp\v5.3.1\esp-idf`
- **开发板**: ESP32-S3-DevKitC（模组 N16R8，8MB 八线 PSRAM）
- **CAN 收发器**: SIT1042AQT/3（STB 接地，VCC 5V，**VIO 接 3.3V**）

## 项目功能
1. **WiFi SoftAP** (设备自己发布热点 SDLG-CAN-WIFI，密码 12345678)
2. **CAN 总线监控** (TWAI 驱动, 250kbps, NORMAL 模式)
3. **网页 CAN 工具** (HTTP Server，表格显示收发 CAN 消息)
4. **曲线查看** (详情视图：前 4 字节转整数曲线；顶层新页签：电压/电流双 Y 轴曲线)
5. **PSRAM 记录仪** (录制 0x18FF0182/0x18FF0282 两报文，录满即停，CSV 物理值导出)
6. **状态灯** (WS2812 GPIO48：无客户端连接=红灯常亮，有客户端=炫彩)

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
- 多文件架构：`main.c`, `can.c`, `can_logger.c`, `signal_decode.c`, `wifi.c`, `web_server.c`, `web_page.h`, `led.c`
- TWAI 配置：`TWAI_MODE_NORMAL`，250kbps，TX=GPIO5，RX=GPIO4
- WiFi SoftAP 模式发布热点 SDLG-CAN-WIFI（密码 12345678），IP 192.168.4.1，mDNS: `can-monitor.local`
- 网页 200ms 轮询：监控表 + 详情曲线/Table + 顶层「电压电流曲线」页 + 录制控制/状态显示

## PSRAM 记录仪 (2026-09-18)

### 架构
- **接收链路** `can.c can_rx_task()`：TWAI 收帧 → 128 条内存环形缓冲（监控用）→
  频率统计 → `can_log_write()`（PSRAM 记录，仅 ID 0x18FF0182/0x18FF0282）→
  若是 0x18FF0282 再 `sig_decode_bus_vi()` + `sig_vi_push()`（实时曲线数据源）
- **录制缓冲** `can_logger.c`：`heap_caps_malloc(6MB, MALLOC_CAP_SPIRAM)`，
  约 314000 条（20 字节/条）。**录满自动停止**（不覆盖、不阻塞），dropped 计数；
  PSRAM 分配失败则 `psram_ok=false` 安全区降级——Record 返回 "PSRAM not available"，
  监控功能完全不受影响。单写者模式：仅 RX 任务写，读侧（status/export）走 mutex 快照
- **信号解码** `signal_decode.c/h`：
  - `SIG_LITTLE_ENDIAN 1` 宏控制 16 位原始值字节序（协议表未标注，实测曲线不对切 0）
  - 电流哨兵值 0x2710 = 「U 相电流零漂故障」，前端跳过不画，CSV 仍导出
  - `sig_vi_push/sig_vi_snapshot`：1200 点实时 V/I 环形缓冲（不依赖录制开关）
- **导出** `web_server.c api_export_handler()`：每次请求实时 `can_log_get_buffer()`
  取基址+条数（禁止开机缓存——曾因缓存条数 0 导致 CSV 只有表头），
  64 条/批 memcpy 后格式化，4KB 行缓冲攒半刷 `httpd_resp_send_chunk`，批量间 delay 10ms
- **前端状态**：录制状态并入 `/api/messages` 的 `rec` 字段（顶层 `recIds` 未采用），
  不新增轮询请求

### CSV 列
`no,timestamp_ms,id,torque,speed_rpm,fault_code,fault_level,current_A,voltage_V`
（0x18FF0182 行填转矩/转速/故障，0x18FF0282 行填电流/电压，电流/电压为物理值一位小数）

### 容量
两报文合计 120 帧/秒：6MB ÷ 20B ≈ 31 万条 ≈ **44 分钟**。如需更长可将
`can_msg_entry_t`（20B）压缩为 `ts4+tag1+data8+flag1 =14B`（约 63 分钟），未实施。

### 网页接口
- `/api/messages` 增加 `rec:{on,cnt,cap,drop,ms,psram}` 和 `vi:[{t,c,v,f}]`
- `POST /api/rec/start|stop|clear`；`GET /api/export`（CSV）
- `max_uri_handlers` 已 8→12（现有 4 + 新增 4 个路由）

### 上电验证要点
串口 monitor 应出现：
1. `esp_psram: Found 8MB PSRAM device`（PSRAM 硬件 OK）
2. `can_log: PSRAM log buffer: ... bytes, ~314000 entries`（录制缓冲就绪）
若见 `can_log: Start failed: PSRAM not allocated...` → PSRAM 没起来，查下述坑 5/6/7。

## 构建注意事项（踩过的坑）

1. **`build/` 缓存绝对路径**：CMakeCache 写死项目绝对路径，项目目录一旦改名或移动
   （本项目从 `led_strip_rmt_ws2812` 改名而来），旧 `build/` 会报
   `CMAKE_C_COMPILER not found`。**改动目录名后必须删掉 `build/` 重新构建**。
2. **确认改动真的编进去了**：怀疑"代码没生效"时，先比对时间戳——
   `ls -la build/can_monitor.bin` 应新于 `main/` 下的源文件。曾因烧了旧固件而白排查一轮。
3. **前端改动后浏览器要强刷**：`web_page.h` 是编进固件的，但浏览器会缓存页面，
   烧录后需 `Ctrl+Shift+R` 才能看到新界面。
4. 本机 Git Bash 下 `idf.py` 会因检测到 `MSYSTEM` 拒绝运行（shell profile 每次都会
   重新注入该变量，`unset` 无效），需在 Python 进程内 `os.environ.pop('MSYSTEM')`
   后再调用 `idf.py`。
5. **PSRAM 配置必须改 `sdkconfig.defaults`，不能手改 `sdkconfig`**（2026-09-18 踩坑）：
   手工往 `sdkconfig` 中间插入 SPIRAM 配置块，且块里混有 v5.3.1 不存在的符号
   （`CONFIG_SPIRAM_USE_HEAP` 等），构建时 kconfig 重写 sdkconfig 直接把整块丢弃 →
   运行时无 PSRAM。正确做法见 `sdkconfig.defaults` + 删除 sdkconfig 全量重建。
6. **PSRAM 依赖 esp_psram 组件，最小组件构建不包含它**（同日第二坑）：
   顶层 `CMakeLists.txt` 用 `set(COMPONENTS main)` 只构建 main 及其依赖，kconfig 报
   `unknown kconfig symbol 'SPIRAM'`。必须改为
   `set(COMPONENTS main esp_psram)` 并把 `esp_psram` 加进 main 的 REQUIRES。
7. **`sdkconfig.defaults` 里不能写中文注释**：Windows 下 confgen 用 GBK 解码该文件，
   UTF-8 中文会触发 `UnicodeDecodeError: 'gbk' codec can't decode`，
   CMake 配置中断。defaults 文件保持纯 ASCII。
8. **改完 sdkconfig.defaults 后删掉旧 `build/config/`（或删 build）再编译**，
   否则可能沿用旧配置缓存。
9. **PowerShell 误报编译错误**：cmd 里 `call ...\export.bat && idf.py build` 时，若
   cmake/esptool 往 stderr 打印 NOTICE 等内容，PowerShell 会包装成
   `NativeCommandError` 报错，实际构建可能已成功——以 `idf.py` 最后输出为准。

## 曲线查看 (2026-09-11)

点主表格任意一行 → 详情视图，默认 `Chart` 曲线，右上角可切 `Table` 看原始报文。
前端纯 Canvas 手绘，**不依赖任何 CDN**（设备热点没有外网，引外部库会加载失败）。

- **取值**：每条报文的**前 4 字节**拼成 32 位整数作 Y 值，横轴为相对第一条报文的秒数
- **字节序**：默认大端（首字节为最高位，CAN 信号常见约定），可切小端；
  勾选有符号后按 `int32` 解释（`FF FF FF FF` = −1）
- DLC 不足 4 字节按实际字节数处理，不做符号扩展
- 最多绘制最近 1200 点（`CHART_MAX_POINTS`）；渲染在 `renderChart()`，
  取值在 `dataToInt()`，坐标轴步长在 `niceStep()`
- 200ms 轮询会重绘，曲线自动向左滚动，始终显示最新数据

## 关键代码位置
| 文件 | 说明 |
|------|------|
| `main/can.c` | TWAI 驱动初始化、RX 任务（挂接记录/解码钩子）、告警处理、发送 API |
| `main/can_logger.c/.h` | PSRAM 录制缓冲：过滤 0x18FF0182/0x18FF0282、录满即停、buffer/status API |
| `main/signal_decode.c/.h` | 电机/母线报文信号解码、字节序宏、1200 点实时 V/I 环形缓冲 |
| `main/wifi.c` | WiFi SoftAP 发布、客户端计数、mDNS |
| `main/led.c` | WS2812 状态灯（无客户端=红，有客户端=炫彩） |
| `main/web_server.c` | HTTP 路由：/ /api/messages /api/send /api/clear /api/rec/* /api/export |
| `main/web_page.h` | 嵌入式 HTML/CSS/JS 网页（监控表 + 双 Y 轴电压电流曲线 + 录制控制） |
| `main/main.c` | 入口：NVS → WiFi → CAN → **can_log_init →** HTTP Server → LED |
| `CMakeLists.txt`（顶层） | `set(COMPONENTS main esp_psram)` — esp_psram 必须显式列出 |
| `sdkconfig.defaults` | PSRAM(OCT/80M) + Flash(DIO/80M/16MB) 的 kconfig 默认值 |
