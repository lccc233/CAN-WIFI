# CAN-WIFI 串口协议

适用：本仓库 ESP32-S3 邮件版固件。协议实现为 `main/serial_cli.c`，WiFi 配置见 `main/wifi.c`，邮箱配置见 `main/mail_sender.c`。

## 1. 连接与行格式

| 项目 | 约定 |
|---|---|
| 接口 | UART0（板载 USB 转 UART）或 ESP32-S3 原生 USB-Serial/JTAG；两口使用独立输入缓冲 |
| 串口参数 | 115200 波特率、8 数据位、无校验、1 停止位（8N1），无硬件流控；原生 USB 的波特率设置不决定 USB 传输速率 |
| 编码 | UTF-8；命令名为小写 ASCII，区分大小写 |
| 请求 | 一行一个命令：`命令[空格参数]`，以 CR (`0x0D`)、LF (`0x0A`) 或 CRLF 结束 |
| 响应 | 文本行或 `status` 的单行 JSON；固件使用 CRLF 输出，主机应兼容空行和串口驱动换行转换 |
| 最大请求长度 | 2047 字节，不含行结束符；超长整行拒绝执行，返回 `命令过长，已拒绝执行` |
| 参数解析 | 跳过命令前和参数前的 ASCII 空格；参数内部及结尾的空格保留；不解析 shell 引号或转义 |
| 输入编辑 | Backspace (`0x08`) / Delete (`0x7F`) 删除一个字节；其他控制字符（如 TAB）忽略 |
| 输入回显 | 普通命令原样回显，包括 `pass` 的密码；`mailauth` 参数以星号回显 |

空行不执行命令。UTF-8 中文字符可占多个字节，参数长度按字节计算，退格也是按字节处理。
例如 `ssid My WiFi` 保存名称 `My WiFi`，`ssid "My WiFi"` 会把双引号也保存到名称中。
邮箱命令不要在地址后追加空格。

输出包含输入回显、启动日志、WiFi/CAN 日志和异步邮件检查结果。协议没有请求编号、统一 ACK 或二进制帧头。
建议上位机逐条发送命令并等待对应结果；读取时按行累积，忽略回显与无关日志。
两个串口可分别输入，但响应通过控制台输出，不用于隔离多个客户端的会话。

CLI 每 20ms 轮询硬件 FIFO，每次最多读取 64 字节。长 `mailauth` 命令建议每次写 32 字节、间隔至少 30ms，
避免一次高速写入整行造成输入丢失。主机接收缓冲建议至少 2048 字节，并持续读取。
打开串口应避免意外触发 DTR/RTS 复位；USB-Serial/JTAG 是否需要 DTR 取决于主机驱动。
烧录器、monitor 和上位机不要同时占用同一 COM 口。

## 2. 命令总表

| 命令 | 用途 | 保存与生效 |
|---|---|---|
| `help` / `?` | 列出命令 | 只读 |
| `info` | 输出可读的 WiFi、CAN、运行时间 | 只读，多行文本 |
| `status` | 输出 WiFi、CAN、邮箱状态 | 只读，单行 JSON |
| `mode ap` | 切换设备热点模式 | 保存；模式变化时约 800ms 后重启 |
| `mode sta` | 切换连接路由器模式 | 保存；模式变化时约 800ms 后重启 |
| `ssid <名称>` | 设置 STA 的 WiFi 名称 | 保存；STA 模式下立即重连 |
| `pass <密码>` | 设置 STA 的 WiFi 密码 | 保存；STA 模式下立即重连 |
| `ip` | 查看 IP 配置规则 | 只读 |
| `ip auto` | 自动绑定当前网段的 `.250` 地址 | 保存；在线 STA 重连后生效 |
| `ip <IPv4地址>` | 固定 STA IP | 保存；在线 STA 重连后生效 |
| `mail` | 查看发件/收件邮箱和授权配置状态 | 只读，多行文本 |
| `mailto <邮箱地址>` | 设置单个收件邮箱 | 保存；后续邮件使用新地址 |
| `mailauth <JSON>` | 导入指定发件邮箱的 OAuth 授权 | 保存；发送或检查进行中拒绝导入 |
| `mailcheck` | 检查 HTTPS、邮箱身份及自动续期 | 异步；可能更新并保存轮换的授权，不发邮件 |
| `reboot` | 重启设备 | 约 300ms 后重启 |

WiFi 配置保存到 NVS 命名空间 `wificfg`；邮箱配置保存到 `mailcfg`。
断电或常规固件烧录后保留，擦除 NVS 后恢复默认值。
命令确认接受配置不代表 WiFi 已重连成功；应随后查询 `status` 的 `link` 和 `ip`。

当前 CAN 参数为 TX=GPIO5、RX=GPIO4、250kbps、NORMAL 模式，串口不提供修改引脚、波特率、发送 CAN、启动记录或发送 CSV 的命令。
CSV 邮件通过网页记录后点击信封发送，或调用 README 中的 Web API。

## 3. `status`：机器可读状态

请求：`status` 加行结束符。响应示例（示例值，不代表实时设备状态）：

```json
{"mode":"sta","link":true,"ssid":"My WiFi","rssi":-50,"channel":6,"ap_clients":0,"ip":"192.168.101.250","netmask":"255.255.255.0","gw":"192.168.101.1","mac":"34:85:18:8E:EE:80","ipmode":"auto","static_ip":"","mdns":"can-monitor","uptime_s":120,"can_ring":0,"can_total":100,"mail_from":"espdata@agent.qq.com","mail_to":"lichen1435374410@163.com","mail_configured":true,"twai":"RUNNING","tec":0,"rec":0}
```

| 字段 | JSON 类型 | 含义 |
|---|---|---|
| `mode` | string | `sta` 或 `ap` |
| `link` | boolean | STA 已获得 IP，或 AP 热点已运行；不表示互联网可达 |
| `ssid` | string | STA 保存的 WiFi 名称，或本机 AP 名称 |
| `rssi` | integer | STA 信号强度，单位 dBm；AP、未连接或未取得信号值时为 0 |
| `channel` | integer | 当前 WiFi 信道；未取得时可为 0 |
| `ap_clients` | integer | AP 已连接客户端数量；STA 为 0 |
| `ip` | string | 当前接口 IP；未取得时可能为 `0.0.0.0` 或空字符串 |
| `netmask` | string | 当前接口掩码；未取得时可能为 `0.0.0.0` 或空字符串 |
| `gw` | string | 当前接口网关；未取得时可能为 `0.0.0.0` 或空字符串 |
| `mac` | string | 当前模式接口的 MAC，六组大写十六进制，以冒号分隔 |
| `ipmode` | string | STA IP 配置规则：`auto` 或 `static`；AP 下也返回保存的 STA 规则 |
| `static_ip` | string | 保存的固定 IP；自动规则通常为空字符串 |
| `mdns` | string | 主机名 `can-monitor`，访问地址 `http://can-monitor.local` |
| `uptime_s` | integer | 本次启动后的运行秒数 |
| `can_ring` | integer | 当前实现查询时没有复制快照，因此固定为 0；不要用于判断是否收到 CAN 报文 |
| `can_total` | integer | CAN 接收累计计数；重启或 Web 清空消息缓冲后归零，不是 CSV 记录点数 |
| `twai` | string / null | `STOPPED`、`RUNNING`、`BUS_OFF`、`RECOVERING`；未知驱动状态为 `?`；无法读取驱动时为 `null` |
| `tec` | integer / null | CAN 发送错误计数；无法读取驱动时为 `null` |
| `rec` | integer / null | CAN 接收错误计数；无法读取驱动时为 `null` |
| `mail_from` | string | 固定发件邮箱 `espdata@agent.qq.com` |
| `mail_to` | string | 当前收件邮箱，默认 `lichen1435374410@163.com`，可用 `mailto` 修改 |
| `mail_configured` | boolean | 已保存非空 OAuth client ID 和 refresh token 时为 `true` |

`mail_configured` 不保证凭据未被撤销、互联网可达或邮件可以投递；使用 `mailcheck` 验证。
`status` 不返回密码、client ID、access token 或 refresh token，也不触发联网、续期或发送邮件。
字段顺序不作为协议约定；上位机应按键名解析，并忽略新增字段。旧固件可能没有邮箱字段。

## 4. 设备与 WiFi 命令

### `help` / `?`、`info`

`help` 和 `?` 等价，返回以 `命令列表:` 开始的多行帮助。
`info` 返回以 `======== 设备状态 ========` 开始的多行文本，包含模式、SSID、连接状态、信道、IP、掩码、网关、MAC、IP 规则、mDNS、运行时间、CAN 缓冲及 TWAI 错误计数。
邮箱信息使用 `mail` 或 `status` 查询；上位机优先解析 `status`，不要依赖 `info` 文本排版。

### `mode ap` / `mode sta`

成功示例：`已切换为 AP 模式并保存，设备即将重启生效...`。
模式未变化：`当前已是 AP 模式` 或 `当前已是 STA 模式`，不重启。
参数缺失或错误：`用法: mode ap | mode sta`。

AP 默认名称 `CAN-Monitor-XXXX`（末四位来自 MAC），密码 `12345678`，IP `192.168.4.1`。
首次未保存配置时为 STA 模式，默认 SSID `ABCDEF`、密码 `A12345678`。
AP 模式可访问设备网页，但不能通过该邮件模块访问互联网发信。

### `ssid <名称>` / `pass <密码>`

SSID 长度 1～32 字节，密码长度 8～63 字节。命令没有外层引号语法。
两项分别更新，另一项保持当前保存值；AP 模式下保存，切回 STA 后使用。

| 请求/结果 | 返回 |
|---|---|
| `ssid My WiFi` 成功 | `SSID 已保存为 "My WiFi"，正在重连...` |
| `ssid` 无参数 | `用法: ssid <WiFi名称>` |
| SSID 设置失败 | `设置失败：SSID 需 1~32 字节`（也可能由配置保存失败导致） |
| `pass MySecret123` 成功 | `密码已保存，正在重连...` |
| 密码长度不合法 | `设置失败：密码需 8~63 个字符`（实际按字节计算） |
| 密码保存失败 | `设置失败` |

`pass` 输入由设备明文回显；终端本地回显关闭也不能屏蔽设备回显。

### `ip` / `ip auto` / `ip <IPv4地址>`

`ip` 输出 `当前规则: 自动绑定同网段 xxx.xxx.xxx.250` 或 `当前规则: 固定 IP <地址>`，
随后输出 `用法: ip auto | ip <x.x.x.x>`。

`ip auto` 成功：`已切换为自动 .250，正在重连绑定...`；保存失败：`保存失败`。
自动规则先取得 DHCP 地址，再按 DHCP 掩码将地址绑定到当前网段的主机号 250，保留 DHCP 网关、掩码和 DNS。
常见 `/24` 网络中为 `xxx.xxx.xxx.250`。

`ip 192.168.1.250` 成功：`已设置固定 IP 192.168.1.250，正在重连绑定...`。
固定规则使用 `/24` 掩码、同网段 `.1` 网关，DNS 沿用 DHCP。
地址解析或保存失败：`IP 地址无效，示例: ip 192.168.1.250`。
应使用完整点分十进制 IPv4，配置与路由器实际网段一致、未被其他设备占用的地址。
这三条命令配置的是 STA 地址规则，AP 地址保持 `192.168.4.1`。

### `reboot`

返回 `设备即将重启...`，随后重启。启动会输出日志，设备就绪后重新请求 `status`。
主机应处理 USB 端口可能重枚举的情况，也应兼容端口句柄仍有效、启动日志从原连接继续输出的情况。

## 5. 邮箱命令

### `mail`

已配置时返回：

```text
发件邮箱: espdata@agent.qq.com
收件邮箱: lichen1435374410@163.com
授权配置: 已配置（发信时验证并自动续期）
```

未配置时第三行为 `授权配置: 未配置，请使用 mailauth 导入`。收件地址以实际保存值为准。

### `mailto <邮箱地址>`

示例：`mailto someone@example.com`。
成功：`收件邮箱已保存为 someone@example.com`。
失败：`收件邮箱设置失败：<ESP错误名称>`，如 `ESP_ERR_INVALID_ARG` 或 NVS 错误。

支持单个 ASCII 邮箱地址，长度 1～127 字节；必须包含一个 `@`、非空本地部分、域名部分中的点，结尾不能是点。
不接受空格、中文地址、显示名称或多个收件人。该本地格式检查不验证邮箱是否存在。
修改后 `mail` 和 `status.mail_to` 立即显示新地址。已经启动的邮件任务使用启动时的地址快照。

### `mailauth <JSON>`

首次在电脑使用官方 CLI 完成 OAuth 授权，再向设备导入授权；之后 ESP32-S3 独立续期和发送。
操作步骤见 [README 首次授权](../README.md#首次授权与发送-csv-附件)。推荐使用项目工具：

```powershell
python tools/provision-mail.py --port COM8
```

直接导入的请求格式（占位符需替换为真实授权，不要公开真实凭据）：

```text
mailauth {"email":"espdata@agent.qq.com","client_id":"<OAuth client ID>","refresh_token":"<refresh token>"}
```

整个 JSON 必须在同一行。三个字段均为字符串，字段名区分大小写：

| 字段 | 限制 |
|---|---|
| `email` | 必须精确等于 `espdata@agent.qq.com` |
| `client_id` | 非空，最多 127 字节 |
| `refresh_token` | 非空，最多 1023 字节 |

成功：`MAILAUTH OK：邮箱授权已保存`。
失败：`MAILAUTH ERROR：<ESP错误名称>（需包含 email/client_id/refresh_token，发送期间不能更新）`。
无效参数通常为 `ESP_ERR_INVALID_ARG`；发送或 `mailcheck` 期间为 `ESP_ERR_INVALID_STATE`；也可能返回内存或 NVS 错误。
导入只校验结构、发件地址和长度，不联网验证凭据；导入后用 `mailcheck` 检查。
禁用终端本地回显，避免终端自行显示凭据；设备的星号回显只保护设备输出。

### `mailcheck`

前提：STA 已连接可上网的路由器，已导入授权，且没有发送或检查任务正在进行。
HTTPS 证书校验还需要正确的系统 UTC 时间，打开设备网页可进行浏览器授时。

启动应答：`正在验证设备邮箱连接…`。
稍后独立输出以下之一（可能夹杂运行日志）：

```text
MAILCHECK OK：设备 HTTPS 连接、邮箱身份和自动续期验证通过（未发送邮件）
MAILCHECK ERROR：<失败原因>
```

无法启动：`邮箱验证未启动：<ESP错误名称>（请检查 STA 联网、授权配置或正在发送的邮件）`。
状态不满足一般为 `ESP_ERR_INVALID_STATE`，任务分配失败可能为 `ESP_ERR_NO_MEM`。
检查过程中会刷新 OAuth 并保存轮换的 refresh token，但不发送测试邮件。
主机应等待 `MAILCHECK OK` / `MAILCHECK ERROR` 作为完成标志；网络请求可能耗时数十秒，启动应答不表示成功。
超时不能推断任务已停止；避免立即重复请求或在任务完成前重启设备。

## 6. 错误与上位机示例

未知命令返回 `未知命令 "<命令>"，输入 help 查看帮助`。
参数错误返回对应命令的用法或错误文本，不会统一输出 JSON 错误对象。
`ESP_ERR_*` 名称表示底层错误类别，完整人类可读文本可能随固件修改；`status` 用 JSON 字段解析。

下面用 pyserial 读取一次状态，不修改任何配置。端口号以设备管理器为准；示例 COM8 是当前设备端口。
串口终端关闭后运行，安装依赖 `python -m pip install pyserial`：

```python
import json
import time
import serial

port = serial.Serial()
port.port = "COM8"
port.baudrate = 115200
port.timeout = 0.2
port.write_timeout = 2
port.dtr = False
port.rts = False

with port:
    port.reset_input_buffer()
    port.write(b"status\r\n")
    pending = bytearray()
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        pending.extend(port.read(4096))
        while b"\n" in pending:
            line, _, remainder = pending.partition(b"\n")
            pending = bytearray(remainder)
            try:
                state = json.loads(line.decode("utf-8").strip())
            except (UnicodeDecodeError, json.JSONDecodeError):
                continue  # 忽略回显、空行和日志；跨 read 的半行保留到下次
            if not isinstance(state, dict) or "mode" not in state:
                continue
            print("WiFi:", state["mode"], state["link"], state["ip"])
            print("发件邮箱:", state.get("mail_from", "旧固件未提供"))
            print("收件邮箱:", state.get("mail_to", "旧固件未提供"))
            print("授权已配置:", state.get("mail_configured", "旧固件未提供"))
            raise SystemExit(0)
    raise TimeoutError("未收到完整 status JSON，请检查端口、设备是否启动及连接设置")
```
