# 交接文档：ESP32-S3 CAN Bus Monitor

## 项目信息
- **路径**: `C:\Users\liche\esp32s3\CAN-WIFI`（原名 `led_strip_rmt_ws2812`）
- **主文件**: `main/main.c`, `main/can.c`, `main/can_logger.c`, `main/signal_decode.c`, `main/wifi.c`, `main/web_server.c`, `main/web_page.h`, `main/led.c`
- **ESP-IDF 版本**: 5.3.1，路径 `C:\Users\liche\esp\v5.3.1\esp-idf`
- **开发板**: ESP32-S3-DevKitC（模组 N16R8，8MB 八线 PSRAM）
- **CAN 收发器**: SIT1042AQT/3（STB 接地，VCC 5V，**VIO 接 3.3V**）

## 项目功能
1. **WiFi STA** (连接现有路由器 `lc`，密码 12345678，断线自动重连，mDNS: can-monitor.local)
2. **CAN 总线监控** (TWAI 驱动, 250kbps, NORMAL 模式)
3. **网页 CAN 工具** (HTTP Server，表格显示收发 CAN 消息)
4. **曲线页** (顶层页签：自定义曲线=任意 ID 用户定义信号，每信号一条曲线带；详情视图仅剩原始报文表)
5. **信号解码** (设备端仅 /api/export CSV 物理值列用；页面曲线全部浏览器解码)
6. **状态灯** (WS2812 GPIO48：未连上路由器=红灯常亮，连上=炫彩)

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
- WiFi STA 模式连接路由器 `lc`（密码 12345678）；**固定 IP**：`wifi.h`
  `WIFI_STA_STATIC_IP=1`（默认 192.168.1.250/网关 192.168.1.1，停 DHCP），
  置 0 退回 DHCP；mDNS: `can-monitor.local`
- 网页 200ms 轮询：监控表 + 详情曲线/Table + 顶层「电压电流曲线」页 + 录制控制/状态显示

## PSRAM 记录仪 (2026-09-18)

### 架构
- **接收链路** `can.c can_rx_task()`：TWAI 收帧 → 128 条内存环形缓冲（监控用）→
  频率统计 → `can_log_write()`（PSRAM 记录，仅 ID 0x18FF0182/0x18FF0282）。
  （2026-09-22 第三轮起 RX 任务不再挂实时曲线钩子——信号曲线页已移除，见下文）
- **录制缓冲** `can_logger.c`：`heap_caps_malloc(6MB, MALLOC_CAP_SPIRAM)`，
  约 314000 条（20 字节/条）。**录满自动停止**（不覆盖、不阻塞），dropped 计数；
  PSRAM 分配失败则 `psram_ok=false` 安全区降级——Record 返回 "PSRAM not available"，
  监控功能完全不受影响。单写者模式：仅 RX 任务写，读侧（status/export）走 mutex 快照
- **信号解码** `signal_decode.c/h`（仅 /api/export CSV 物理值列使用）：
  - `SIG_LITTLE_ENDIAN 1` 宏控制 16 位原始值字节序（协议表未标注，实测不对切 0）
  - 电流哨兵值 0x2710 = 「U 相电流零漂故障」，CSV 仍导出原始值
  - （原 1024 点实时曲线环形缓冲与电机缓存已随曲线页移除，曾修过
    1200→1024 非 2 幂掩码 bug——历史教训保留在此）
- **导出** `web_server.c api_export_handler()`：每次请求实时 `can_log_get_buffer()`
  取基址+条数（禁止开机缓存——曾因缓存条数 0 导致 CSV 只有表头），
  64 条/批 memcpy 后格式化，4KB 行缓冲攒半刷 `httpd_resp_send_chunk`，批量间 delay 10ms
- **前端状态**：`rec` 字段仍并入 `/api/messages` 响应，但页面已无录制 UI
  （录制改由自定义曲线页的前端记录器承担；`/api/rec/*` 留作 curl 备用）

### CSV 列
`no,timestamp_ms,id,torque,speed_rpm,fault_code,fault_level,current_A,voltage_V`
（0x18FF0182 行填转矩/转速/故障，0x18FF0282 行填电流/电压，电流/电压为物理值一位小数）

### 容量
两报文合计 120 帧/秒：6MB ÷ **18B** ≈ 37 万条 ≈ **52 分钟**（9-20 优化：`can_msg_entry_t`
packed 20B→18B，去掉对齐 padding）。如需更长可再上条目压缩（ts4+tag1+data8+flag1=14B，
约 67 分钟），未实施。

### 内部优化记录 (2026-09-20 第二批)
- **A1**：`can_rx_task` 写监控环形缓冲改为持锁（原与 clear 竞态）
- **A2**：录制中拒绝 `/api/rec/clear`（防导出数据集被覆盖），新增 `can_log_is_recording()`
- **A3**：`/api/time` tz 钳位 ±840；**A4**：录制缓冲录满停止走 mutex
- **B1**：`can_msg_entry_t` 18 字节（见上）；**B3**：TWAI 告警日志 1s 去抖；
  **B4**：`LOG_DEFAULT_LEVEL_INFO` + 无颜色（需删 sdkconfig 重建生效）
- **C1**：httpd stack 12KB，`/api/messages` vi 数组 2KB 攒发（syscall ÷10）；
  **C2**：`Cache-Control: no-store`；**C3**：poll 忙闸（并发第二请求回 `{"busy":1}`，
  前端静默跳过）；**C4**：`WIFI_PS_NONE`
- **D1**：`web_page.h` 拆为 `web_head.h / web_body.h / web_js.h`，
  `web_page.h` 仅剩拼接宏 `#define INDEX_HTML PAGE_HEAD PAGE_BODY PAGE_JS`
- **D2**：`/api/send` 改 cJSON 解析（cjson 组件加入 REQUIRES，行为兼容旧字段）；
  **D3**：web_server 常量集中顶部；**D4**：sig_decode 加 `assert(dlc<=8)`

### 网页接口
- `/api/messages` 开头带 `clk:{sync,boot,ep}`（浏览器授时状态），随后
  `rec:{on,cnt,cap,drop,ms,psram}`、`total`、`freqs`、`messages`
  （2026-09-22 第三轮起不再含 `vi` 段）；服务端 poll 忙闸期间秒回 `{"busy":1}`，
  忙闸带 **3 秒看门狗**（防止客户端中途断开导致忙闸标志永不复位）
- `POST /api/time`（epoch_ms + tz 分钟，tz 钳位 ±840）/ `POST /api/rec/start|stop|clear`
- `GET /api/export`（CSV，time 列授时后为真实时间）；`max_uri_handlers` 已 8→12

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
10. **改 HTML 拆分段（web_body.h 等）时 div 配对极易破坏**（2026-09-22 踩坑）：
    删 Chart 区块时把 `detail-header` 的闭合 `</div>` 一并删掉，浏览器把其后所有元素
    （backRow/sendPanel）解析成隐藏的 viewDetail 子节点，面板“消失”却查不到原因。
    **删完必须校验 div 开闭数量**（node 脚本统计 `<div` vs `</div>` 应为 0 残留），
    并用 DevTools Console 检查链条 `getElementById().parentElement` 是否落在预期节点。
11. **web_js.h 是 CRLF，PowerShell 所见即所付**：多行 JS 替换锚点会因换行符
    精确匹配失败而“静默不生效”（String.replace 不报错）。批量 JS 手术用 node 脚本
    以 `indexOf` 定位 + 切片拼接，或先探测 CRLF 再决定锚点写法。
12. **JSON 手拼格式串要走模拟拼接验证**（2026-09-22 踩坑两次）：
    引入 `clk` 字段时 rec 串残留旧版顶层 `{` 导致整条 JSON 非法（Bad response）；
    vi 点加 q/r/m 时自动替换把格式串截断。每次改 `/api/messages` 的 format 后，
    用 node 按同样 format 拼一份数据跑 `JSON.parse` 再提交。

## 页面结构现状 (2026-09-22)

- 主表格按 ID **升序**（数值比较，兼容变长 hex）
- 详情视图 = 该 ID 原始报文表（无内部 Chart/Table 页签；内部 per-ID 曲线已按需求删除）
  + 顶部信号定义卡片（`#sigList` + `#addSigBtn`，编辑走 `#formMask` 模态框）
- 头部控制条只剩 Clear + 状态灯 + 消息计数（设备录制 Record/Export CSV 已移除；
  `/api/rec/*` 与 `/api/export` 保留为 curl/URL 备用）
- 发送面板 = `#sendPanel` `position:fixed` bottom:0，仅 CAN Monitor 显示；
  详情页底部换 `#backRow`（同一样式，fixed），两者各自随视图切换显隐
- 自定义曲线页 = 顶层第 2 页签 `#viewCharts`（控制条 + `#chartList` 每信号一条 strip）；
  添加/编辑信号模态框 `#formMask`；`#toast` 轻提示（bottom 86px 避开 sendPanel）
- **电压电流曲线页已整体移除**（2026-09-22 第三轮）：其 I/U/T/n 功能由自定义曲线页
  预置信号等价覆盖；连带删除服务端 `/api/messages` 的 `vi` 数据块、
  signal_decode 的 1024 点环形缓冲 + 电机缓存、can.c RX 钩子（见下节）
- 自定义曲线页 = 顶层第 3 页签 `#viewCharts`（控制条 + `#chartList` 每信号一条 strip）；
  详情页顶部有 `#sig-card` 信号定义面板（`#sigList` + `#addSigBtn`）；
  添加/编辑信号走 `#formMask` 模态框（f_id/f_name/f_start/f_len/f_endian/f_signed/
  f_factor/f_offset/f_unit/f_color）；`#toast` 轻提示（bottom 86px 避开 sendPanel）

## 自定义曲线 (2026-09-22，固件零改动)

- **思路**：`/api/messages` 本就下发每帧原始字节（hex 串），解码在浏览器做完全等价——
  任意 CAN ID 按用户配置（DBC 风格 start/len/endian/signed/factor/offset/unit/color/
  enabled）解析信号并绘制，固件一行不改（web_server.c 0 改动）
- **数据层**：`processMessages()` 在 `total` 变化时把每条新帧按 ID 追加进
  `histById`（`{t,b,d,e}`，每 ID 上限 4000 点）；服务端快照是最近 128 条**重叠**返回，
  用每 ID 水钟 `histLastT`（`t > last` 才收；`t+5000 < last` 视为设备重启清空重来）去重
- **解码器** `extractRaw/decodePoint/sigBytesCovered`：Intel 从 start 位号逐位拼装；
  Motorola 先换算 `bitPos=(start>>3)*8+(7-(start&7))` 从 MSB 逐位移入；
  位区间超出 DLC → null（曲线断线）；`值 = raw×factor+offset`。
  demo 包 11 项单测全通过（移植后追加"默认配置=固件语义"2 项，共 13/13）
- **UI**：Monitor 主表/详情表 `colorizeData()` 按信号颜色给字节加半透明底色+彩色下划线；
  详情页信号面板实时显示最新解码值；Charts 页每信号一条 `drawStrip()` 曲线带
  （独立量程、niceStep(·,4)、10s/30s/60s 窗、暂停冻结重绘）
- **前端记录器** `curveRec*`（勿与头部设备 PSRAM Record `#recBtn` 混淆）：
  Record 时对每个已启用信号在帧到达时采样 `{t,v}` 入 `curveRecData[sigKey]`，
  每信号上限 20 万点（满则一次 splice 掉 2000 条防 shift 卡顿）；
  「导出记录CSV」用指针法按时间归并成宽表 `no,time_rel_ms,Torque(Nm)@0x..,...`
- **配置持久化**：`localStorage['cansignals']`（demo 用 'cansignals_demo'，跨 origin
  不互通，属正常）；「导出配置」下载 JSON；**无导入功能**（按方案约定）
- **默认信号**：与固件 signal_decode.c 协议表一致——Torque/Speed（0x18ff0182，
  Intel start=8/24，signed，offset −3000/−15000）、Current/Voltage（0x18ff0282，
  Intel start=0/16，factor 0.1，Current offset −1000 unsigned）。
  注意哨兵 0x2710 在自定义曲线里 Current 会显示 0.0A、Voltage 1000V（通用解码器
  不识别哨兵；哨兵处理仍只在本项目专用曲线页做）
- **限制**：只积累打开页面后的数据；200ms 轮询快照去重，高速总线偶有丢帧（画曲线够用，
  非示波器级）；二进制增长约 7KB（bin 0xED3D0，分区余 7%——**后续再加大页面需留意分区**）

## 电压电流曲线页移除 + 录制合并 (2026-09-22 第三轮)

- **动机**：自定义曲线页上线后，专用 I/U 双画布页与其功能重叠（预置信号
  Current/Voltage/Torque/Speed 等价覆盖），且页面出现两套 Record/导出 CSV 按钮
- **移除范围（信号曲线链路整体下线）**：
  - `web_server.c`：删 `/api/messages` 的 `vi:[...]` 数据块（每 200ms 轮询省 ~20KB 死数据）
    + `VI_JSON_MAX_POINTS/VI_BATCH_STR` 常量；忙闸 C3 保留（保护 128 条 messages 快照）
  - `signal_decode.c/.h`：删 `sig_vi_point_t`、`sig_vi_push/count/snapshot/get_back`、
    `sig_motor_set/get` + 电机缓存（~14KB RAM 随之释放）；保留 `sig_decode_motor/bus_vi`
    （/api/export CSV 列仍在用）与 `sig_is_record_id`
  - `can.c` RX 任务：删 0x18FF0182/0x18FF0282 的解码/入环钩子与 signal_decode.h include
  - `web_body.h`：删 tabVI/viewVI 两画布；`web_head.h`：删 .chart-wrap/.chart-note/
    .chart-ctl/.rec-status/#exportBtn/#recBtn.recording（recblink 动画保留给 #curveRecBtn）
  - `web_js.h`：删 allVi/drawDual/drawVI/showVI/fmtAxisVal/updateRecUI/lastRecOn/
    设备 recBtn 监听器；currentView 取值收敛为 'main'|'detail'|'charts'
- **行为差异**：曲线数据来源从"服务端 400 点缓冲（页面打开前也有）"变为
  "浏览器 histById（仅打开页面后）"；哨兵 0x2710 在自定义曲线里 Current 显示 0.0A
  （通用解码器不识别哨兵）；设备端固定解码的 `SIG_LITTLE_ENDIAN` 仍影响 /api/export
- **收益**：bin 0xED3D0 → 0xEA210（分区余 9%）、轮询 JSON 减半、RX 任务少两个分支、
  RAM 释放 ~14KB；页面只剩一套 Record/导出 CSV（前端记录器）
- **坑**：删 DOM 元素时必须同步删 JS 里的 `getElementById(...).addEventListener`——
  对 null 调用会抛 TypeError 直接杀死整个脚本（本轮 recBtn/tabVI/exportBtn 三处）
- **坑**：删某个视图（contentVI）时，showMain/showDetail 里对应的
  `contentVI.classList.add('hidden')` 行会被一并清掉，**必须补上对所有剩余视图的
  隐藏**——否则从另一视图返回时该视图残留在屏幕下方（2026-09-22 实际踩坑，
  修复：三处视图切换函数对称互斥隐藏）

## 配置持久化：NVS + 导入 (2026-09-22 第四轮)

- **动机**：信号定义原先只存浏览器 localStorage——换手机/清浏览器数据/iOS Safari
  7 天不访问都会丢，且导出的 JSON 没法恢复
- **设备 NVS 存储**：`GET/POST /api/signals`（web_server.c `api_signals_handler`），
  NVS namespace `webui` key `signals`，存**紧凑 JSON 数组原文**（cJSON 校验后重序列化，
  上限 3500B ≈ 40 个信号）；`max_uri_handlers 12→13`；单 handler 分支 GET/POST
- **同步策略**：
  - 页面每次改配置（saveCfg）→ localStorage 即存 + 500ms 节流 POST 到设备
  - 页面加载时 GET 设备配置：**设备为准**覆盖本地缓存（内容有变才 toast/重绘）；
    设备为空（首次/擦除过）→ 把浏览器现有配置迁移上去
  - 多浏览器并存：**最后保存者生效**（页面只在加载时拉取一次，不实时对账）
- **导入配置**：自定义曲线页新增 `#cfgImportBtn`（`#cfgImportFile` 隐藏 file input），
  FileReader 解析 JSON → 校验每项 id（0x hex）/name → 整组替换 → saveCfg 双写
- **收益**：配置真正断电不丢、换设备不丢（换机后打开页面自动加载设备配置，
  或用导入 JSON 恢复）；bin 0xEA270 → 0xF09C0，**分区仅剩 6%（63KB）——
  后续页面改动需优先考虑体积**

## 关键代码位置
| 文件 | 说明 |
|------|------|
| `main/can.c` | TWAI 驱动初始化、RX 任务（挂接记录/解码钩子）、告警处理、发送 API |
| `main/can_logger.c/.h` | PSRAM 录制缓冲：过滤 0x18FF0182/0x18FF0282、录满即停、buffer/status API |
| `main/signal_decode.c/.h` | 电机/母线报文信号解码（字节序宏）、sig_is_record_id 录制过滤——仅 /api/export CSV 列在用 |
| `main/wifi.c` | WiFi STA 连接/断线重连、连接状态、mDNS |
| `main/led.c` | WS2812 状态灯（无客户端=红，有客户端=炫彩） |
| `main/web_server.c` | HTTP 路由：/ /api/messages /api/time /api/send /api/clear /api/rec/* /api/export |
| `main/web_page.h` | 页面组装宏（`INDEX_HTML` = PAGE_HEAD + PAGE_BODY + PAGE_JS） |
| `main/time_sync.c/.h` | 浏览器授时换算（CSV time 列真实时间 / 曲线标题时间） |
| `main/web_head.h` / `web_body.h` / `web_js.h` | 页面三段源：头/样式+水印、DOM、脚本 |
| `main/main.c` | 入口：NVS → WiFi → CAN → **can_log_init →** HTTP Server → LED |
| `CMakeLists.txt`（顶层） | `set(COMPONENTS main esp_psram)` — esp_psram 必须显式列出 |
| `sdkconfig.defaults` | PSRAM(OCT/80M) + Flash(DIO/80M/16MB) 的 kconfig 默认值 |
