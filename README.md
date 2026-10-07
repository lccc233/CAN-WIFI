# ESP32-S3 CAN Bus Monitor

基于 ESP-IDF 的 CAN 总线监控工具：ESP32-S3 通过 TWAI（CAN 2.0）接收总线数据，以 Web 页面实时展示，支持手动发送 CAN 帧；完整原始帧记录保存在**设备 PSRAM**，浏览器按帧序号分批读取并统一解码，用于实时曲线和 CSV。

## 功能

- **CAN 总线监控**：TWAI 驱动，**250 kbps**，NORMAL 模式，TX=GPIO5，RX=GPIO4
- **Web 实时界面**（HTTP Server，页面 200ms 轮询刷新）：
  - 顶层页签：**CAN Monitor**（按 ID 分组表格）/ **自定义曲线**（任意 ID 自定义信号）
  - 按 ID 分组展示最新消息（**按 ID 从小到大排序**），列：`ID / Count / Freq / DLC / Ext / Data / Last Time`
  - **Freq**：每个 ID 的发送频率（条/秒，基于滚动 1 秒窗口统计）
  - 点击某行进入该 ID 的**原始报文历史表**（自动滚动跟随最新），无内部曲线
  - 发送面板固定在 CAN Monitor 页面底部；进入详情页时隐藏（替换为返回按钮）
  - 支持手动发送任意 CAN 帧、一键清空
- **自定义曲线页**（信号解码和绘图在浏览器实现）：
  - 在详情页用「+ 添加曲线」为**任意 CAN ID** 定义信号：起始位/位长/字节序（DBC 位号，Motorola=MSB 位号、Intel=LSB 位号）/符号/factor/offset/单位/颜色，`值 = raw × factor + offset`
  - 每个信号一条独立曲线带（自动量程 + niceStep 刻度），窗口 10s/30s/60s 可切换，可暂停冻结读数
  - Monitor 主表与详情表中，已配置信号覆盖的**字节按信号颜色高亮**，实时数值随轮询刷新
  - **完整数据记录器**：Record 在设备逐帧保存已启用信号对应 ID 的原始报文，固定本次信号配置；
    网页每批读取最多 128 帧，解码结果共用于曲线和完整记录，停止后自动核对、补齐并确认最终帧数。
    显示“同步完整”后开放「导出记录CSV」和邮件；CSV 每个原始帧一行，保留同时间戳的不同帧，
    列为 `no,time_rel_ms,信号(单位)@ID,...`，其他 ID 或长度不足的信号留空，数值不使用显示缩写。
  - 信号配置**双份持久化**：浏览器 localStorage + **设备 NVS**（断电不丢，换手机打开页面自动从设备拉取；
    「导出配置」备份为 JSON 迷你 DBC，「导入配置」一键恢复）
  - 配置保存明确显示成功/失败，主动清空不会被旧浏览器缓存恢复；录制期间编辑用于下次录制。
  - 录满自动停止并保留已有记录，报告接收丢帧/未知状态；页面刷新或断网恢复可重新读取设备记录。
    PSRAM 为易失存储，设备断电/重启会丢失设备内原始记录。详细容量、恢复和限制见 [记录与导出说明](docs/recording.md)。
- **浏览器授时**：打开页面自动 `POST /api/time` 校准，原始帧备份 CSV（`/api/export`）的 `time` 列为真实时间
  （未授时时回退为开机相对时间 `boot + HH:MM:SS.mmm`）
- **CSV 邮件附件**：自定义曲线页“导出记录CSV”后面的信封按钮，停止并同步完整后发送本次信号宽表 CSV；
  使用与下载共用的 CSV 生成函数。发送时信封黄色慢闪，邮件服务明确接受后绿色，失败红色并提示原因。
  ESP32-S3 独立通过 HTTPS 调用 QQ Agent 邮箱接口，自动刷新 OAuth 凭据，运行时不需要电脑代发。
  发件地址固定 `espdata@agent.qq.com`，默认收件地址 `lichen1435374410@163.com`，串口 `mailto` 可修改并保存。
  附件上限 20MB（同时遵守邮箱账户返回的更低限制），分块 Base64 + SHA-1 流式发送，避免整份附件占用 PSRAM。
- **设备端备用导出**（API）：
  - 网页录制使用同一 PSRAM 记录器，目标 ID 由本次启用信号确定；6MiB 最多约 35 万帧，
    当前两路合计 120 帧/秒时约 48.5 分钟，实际容量由页面显示。
  - 停止后访问 `/api/export` 可异步下载原始帧 CSV：`no,time,id,dlc,ext,data`，
    适合保留源数据；自定义解码宽表使用网页 CSV 下载。
  - 保留无 body 的 `POST /api/rec/start` 旧版双 ID 录制，此模式 `/api/export` 仍导出原有固定物理值列。
- **WiFi 双模式（串口可配，NVS 持久化）**：
  - **STA 模式**（默认）：连接现有路由器/热点，SSID、密码经串口命令设置（默认 `ABCDEF`/`A12345678`）
  - **AP 模式**：设备自建热点 `CAN-Monitor-XXXX`（XXXX=MAC 后 4 位，密码 `12345678`），
    设备 IP 固定 `192.168.4.1`，手机连热点直接访问网页
  - **IP 自动绑定 .250**（默认规则）：DHCP 拿到 IP 后自动改绑为**同网段**的 `xxx.xxx.xxx.250`
    （网关/掩码/DNS 沿用 DHCP 下发值），热点网段变了也无需改代码；也可用串口固定任意 IP
  - `can-monitor.local` 也可访问；断线自动重连（前 5 次立即重试，之后 1s→30s 指数退避）
- **串口配置台**：UART0 与 USB-Serial/JTAG 两个串口均可输入命令（详见「串口配置命令」）
- **状态灯**（WS2812，GPIO48）：**未连上路由器（热点未运行）→ 红灯常亮；连上（拿到 IP）/热点运行 → 炫彩**

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

无需安装开发环境的 Windows 64位独立烧录包：[GitHub Release 下载](https://github.com/lccc233/CAN-WIFI/releases/tag/v2026.10.08-recording)。
附件为 `CAN-WIFI_20261008_RECORDING_Win64.zip`，包含完整记录新版；本地打包输出在 `release/` 目录。
解压后双击 `CAN-WIFI-Flasher.exe`，选择串口即可离线烧录；EXE 内置工具与固件，升级保留配置。
也支持 [乐鑫官方网页烧录](https://espressif.github.io/esptool-js/)，选择包内三个 BIN，地址为 `0x0`、`0x8000`、`0x10000`，
参数 DIO / 80MHz / 16MB，保留配置时不要点击 Erase Flash。
完整说明见 [独立 EXE 与官方网页版烧录方法](docs/offline-flashing.md)。

新版已通过[五分钟 CAN 高负载实机验证](docs/recording-stress.md)：四路正弦与 64 个背景 ID 同时运行，实际发送的全部目标帧及 CSV 完整匹配。

```bash
idf.py -B build-mail -D SDKCONFIG=build-mail/sdkconfig -p <COM口> build flash monitor
```

PSRAM 通过 `sdkconfig.defaults` 启用（OCT 八线 / 80MHz / Flash DIO 80MHz / 16MB），
不要手动改 `sdkconfig`——**手工插入的配置块会被构建系统重写丢弃**（见 HANDOFF）。
烧录走 **UART 模式**：USB 口本身暴露 COM 口，端口号以设备管理器/`idf.py` 枚举为准
（历史配置为 COM6，拔插可能变化），用 esptool 直接烧录，无需 OpenOCD。

邮件版使用 `partitions.csv` 的 3MB 应用分区（起始仍为 `0x10000`），NVS 地址/大小不变。
使用独立 `build-mail/sdkconfig` 从 `sdkconfig.defaults` 生成配置，以免旧 `sdkconfig` 的 1MB 分区覆盖新默认值；
不要沿用移动项目前的旧 `build/` 缓存。首次升级需要一起烧录应用与分区表，无需擦除 NVS。

> **改了目录名或移动过项目？** 先删掉 `build/` 再构建。CMakeCache 会写死项目绝对路径，
> 沿用旧 `build/` 会报 `CMAKE_C_COMPILER not found`。
>
> **改了网页后烧录了却看不到变化？** 浏览器缓存了旧页面，按 `Ctrl+Shift+R` 强制刷新。
>
> **怀疑改动没生效？** 比对时间戳：`build-mail/can_monitor.bin` 应该新于 `main/` 下的源文件。

## 使用

1. 设备上电后按 NVS 保存的配置连网（首次烧录默认 STA 连热点 `ABCDEF`/`A12345678`，自动绑定 192.168.43.250）
   - **注意**：`CONFIG_SPIRAM_MEMTEST=y` 会让上电慢几秒（PSRAM 内存测试），正常
   - 默认配置下设备 IP `192.168.43.250`（无需查日志；热点管理页也会列出已连设备）
   - 换热点/改配置不用重烧固件，用**串口命令**（见下节）
2. 浏览器打开 `http://<设备IP>`（默认 `http://192.168.43.250`）或 `http://can-monitor.local`
3. 在底部发送区填写 ID / DLC / Data 即可向总线发送 CAN 帧（该发送面板只在 CAN Monitor 页显示）
4. **看报表**：点主表格里任意一行 → 该 ID 的原始报文历史表，左上 Back to List 返回
5. **自定义曲线**：详情页「+ 添加曲线」定义信号
   （Motorola 起始位=MSB 位号 byte0 整字节=7；Intel=LSB 位号 byte0=0），
   或直接用**预置的 4 个信号**（Torque/Speed/Current/Voltage）；顶部页签切到
   「自定义曲线」看每个信号一条曲线带（窗口 10s/30s/60s，可暂停）
6. **记录/导出**：自定义曲线页点 `● Record`，设备保存已启用信号对应 ID 的原始帧，网页统一解码。
   停止后等待“同步完整”，再点「导出记录CSV」下载宽表 CSV（Excel/Python 可直接分析）；
   「导出配置」把信号定义备份为 JSON
7. **原始帧备份**（可选）：浏览器直接访问 `http://<设备IP>/api/export`
   下载已停止记录的源数据 CSV（网页录制为原始帧列；旧版无 body 录制为固定物理值列）

### 串口配置命令

`idf.py monitor`（或任意串口终端，115200）连上后**直接敲命令回车**即可，UART0（USB转UART口）
与板载 USB-Serial/JTAG 口都支持输入；`info` 可随时查看设备当前状态。

完整命令、响应格式、`status` 字段和上位机示例见独立文档：[串口协议](docs/serial-protocol.md)。

```
help                命令列表
info                当前状态：模式/SSID/信号/IP/掩码/网关/MAC/CAN状态/运行时间
status              WiFi/CAN/邮箱状态，单行 JSON 输出（供 PC 上位机解析）
mode ap | mode sta  切换热点/STA 模式（保存到 NVS，设备自动重启生效）
ssid <名称>         设置 STA 连接的 WiFi 名称（保存并立即重连，无需重启）
pass <密码>         设置 STA 密码（8~63 字符，保存并立即重连）
ip                  查看当前 IP 配置
ip auto             自动绑定当前网段的 xxx.xxx.xxx.250（默认）
ip <x.x.x.x>        设置固定 IP（如 ip 192.168.1.250）
reboot              重启设备
mail                查看发件/收件邮箱和授权配置状态（不显示凭据）
mailto <邮箱地址>   修改收件邮箱并保存到 NVS
mailauth <JSON>     导入 OAuth 授权，输入凭据以星号回显
mailcheck           验证设备 HTTPS、邮箱身份和自动续期（不发信）
```

所有配置保存于 NVS（命名空间 `wificfg`），断电不丢、重烧固件不丢（擦除 NVS 后回到默认值）。

邮件配置使用独立 NVS 命名空间 `mailcfg`，同样断电保留。

### 首次授权与发送 CSV 附件

1. 按 [QQ Agent 官方配置说明](https://agent.qq.com/doc/cli-setup.md) 安装最新官方 CLI，
   运行 `agently-cli auth login`，在浏览器完成 `espdata@agent.qq.com` 的 OAuth 授权；用 `agently-cli +me` 核对邮箱。
2. 烧录邮件版固件后，用 Windows 上的 Python 和 pyserial 导入授权（串口不要被 monitor/上位机占用）：

   ```powershell
   python tools/provision-mail.py --port COM8
   ```

   该工具读取当前 CLI workspace 的 Windows DPAPI 凭据，仅导入 `email/client_id/refresh_token`，
   不输出凭据、不把它们写进源代码。也支持 `--cli <官方CLI路径>`，或 `--auth-file <本机DPAPI加密配置文件>`。
   其他系统可通过串口 `mailauth {"email":"espdata@agent.qq.com","client_id":"<OAuth client ID>","refresh_token":"<refresh token>"}` 导入。
   不要提交授权文件或把真实凭据发到聊天中。授权被撤销/续期失败时，重新授权并导入。
3. 设备 STA 模式连接能上网的路由器/手机热点，打开设备页面以校准 UTC 系统时间（用于 HTTPS 证书校验）。
   可在串口执行 `mailcheck` 验证设备连接，不会发送测试邮件。
4. 自定义曲线页 Record → Stop → 点击 CSV 导出旁的信封；无需先下载到电脑。
   先停止记录并等待“同步完整”，再发送；发送失败保留数据，不影响之后下载或重试。
5. 修改收件地址：`mailto someone@example.com`；用 `mail` 查看保存结果。

绿色表示服务响应 `queued:true`，已接受投递，不等同于收件人已收到。
网络中断时若显示“发送结果未确认”，先检查收件箱/发件箱再重试，避免重复投递。
邮件模块依据官方 CLI **1.0.18** 的请求预览格式接入；目前没有官方 ESP32 SDK，若 QQ 调整接口需要同步更新。
OAuth 授权仅用于首次配置；之后由设备保存轮换的 refresh token。不要同时用复制的授权在其他客户端反复续期。

不想敲命令？用 **PC 上位机**（同级目录 `../CAN-WIFI-Host/`）：图形界面切换模式、
改 SSID/密码/IP、实时状态面板 + 串口终端。`dist/CANMonHost.exe` 双击即用（新电脑
零依赖），或 `pip install pyserial` 后运行 `canmon_gui.py`，详见其 README。

**手工验证清单**（改 WiFi 相关代码后过一遍）：

1. 上电 `info`：模式/SSID/IP 显示正确，STA 显示信号强度与信道
2. `mode ap` → 自动重启 → 手机能看到热点 `CAN-Monitor-XXXX`，连上后 `http://192.168.4.1` 打开网页，LED 炫彩
3. AP 下 `mode sta` → 重启后回 STA 并按保存的 SSID 重连
4. `ssid 新热点名` + `pass 密码` → 不重启即重连，新热点网段下 IP 为 `xxx.xxx.xxx.250`
5. `ip auto` 换不同网段的热点验证仍得 `.250`；`ip 192.168.1.250` 固定 IP 生效
6. 断电重启：以上配置均保留

### 信号定义（来自协议表）

四路曲线可用 PCAN-USB 正弦测试脚本验证：`python tools/can-sine-test.py`（持续发送，Ctrl+C 停止）。
接线、依赖安装、波形参数和完整运行示例见 [正弦 CAN 测试说明](docs/can-sine-test.md)。

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

- **哨兵值**：电流原始值 `0x2710` 表示“U 相电流零漂故障”。自定义曲线页的通用解码器
  **不识别哨兵**（Current 会显示 0.0A、Voltage 1000V）；网页录制的 `/api/export` 保留原始报文
- **字节序不确定**：自定义信号的幅值方向明显不对时，在「编辑信号」里直接把
  字节序切成 Motorola/Intel 即可（前端解码，无需重编译）
- 旧版无 body 录制的固定解码仍用 `SIG_LITTLE_ENDIAN`（`main/signal_decode.h`，仅影响旧模式 `/api/export` 的物理值列）

## Web API

| 接口 | 说明 |
|------|------|
| `GET /api/messages` | 返回 `{boot,clk:{sync,boot,ep},rec,total,freqs:[{id,f}],messages:[{seq,t,id,dlc,ext,data}]}`；`rec` 含记录编号、容量及采集质量；服务端忙闸期间秒回 `{"busy":1}` |
| `POST /api/time` | 浏览器授时：body `{epoch_ms, tz}`（UTC 毫秒 + 时区偏移分钟） |
| `POST /api/send` | 发送 CAN 帧（body 含 id/dlc/data/extended）；id 越界（标准帧 >0x7FF / 扩展帧 >0x1FFFFFFF）或 dlc 非法时返回 400 |
| `POST /api/clear` | 清空消息缓冲与频率统计 |
| `POST /api/rec/start` | body `{signals:[...]}` 开始完整录制并冻结配置；无 body 保留旧版双 ID 模式 |
| `POST /api/rec/stop` | body `{session}` 原子停止并返回最终帧数/配置，保留数据 |
| `GET /api/rec/status` | 返回当前记录状态和本次固定信号配置 |
| `GET /api/rec/data` | `?session=...&client=...&from=...` 按序号返回最多 128 帧，并续期同步保护 |
| `POST /api/rec/ack` | `{session,client,count}` 确认已停止记录全部处理，返回可重试回执 |
| `POST /api/rec/release` | `{session,client}` 显式释放上一份已停止记录，其他页面同步/导出时拒绝 |
| `POST /api/rec/clear` | 仅允许清空已停止、已释放且无同步/导出读者的记录 |
| `GET /api/signals` | 兼容数组响应；`?meta=1` 返回 `{ok,configured,signals}`，区分未配置与主动清空 |
| `POST /api/signals` | 完整校验配置数组并保存到 NVS；最多 64 条/3500 UTF-8 字节，校验位域、类型、有限倍率/偏移及重复信号；页面显示保存结果 |
| `GET /api/export` | 已停止记录的异步备用 CSV 下载；网页录制为原始帧，旧版无 body 录制为固定物理值列 |
| `POST /api/mail/send` | body 为前端记录 CSV，`X-CSV-Filename` 为 ASCII `.csv` 文件名；异步任务流式发信，服务接受后返回 `{ok:true,message}`，并发发送返回 409；失败保留浏览器记录 |

## 目录结构

```
main/
├── main.c             # 初始化：NVS → WiFi(AP/STA) → CAN → 记录器 → Web → LED → 串口配置台
├── can.c / can.h      # TWAI 驱动、RX 任务、每 ID 频率统计
├── can_logger.c/.h    # PSRAM 完整录制（配置 ID 过滤、会话/序号/同步保护，录满即停）
├── mail_sender.c/.h   # QQ Agent OAuth 续期、邮箱身份验证、CSV 邮件附件流式发送
├── signal_decode.c/.h # 0x18FF0182/0x18FF0282 信号解码（/api/export CSV 物理值列用）
├── wifi.c / wifi.h    # WiFi AP/STA 双模式 + NVS 配置 + IP 自动绑定 .250 + mDNS + 断线重连
├── serial_cli.c/.h    # 串口配置台（mode/ssid/pass/ip/info 命令，UART0 与 USB 串口均可输入）
├── time_sync.c/.h     # 浏览器授时换算（真实时间戳）
├── led.c / led.h      # WS2812 状态灯（红=未连接，炫彩=已连接/热点运行）
├── web_server.c       # HTTP 服务与 JSON API / CSV 导出
├── web_page.h         # 页面组装宏（拼接 web_head/web_body/web_js 三段源文件）
└── web_head.h web_body.h web_js.h   # 前端页面源（HTML 头+样式 / DOM / JS）

sdkconfig.defaults # kconfig 默认值（PSRAM/Flash），改配置改这里，不要手改 sdkconfig
```
