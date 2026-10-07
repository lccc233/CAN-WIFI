# Windows 独立烧录包

下载：[GitHub Release · v2026.10.07-mail](https://github.com/lccc233/CAN-WIFI/releases/tag/v2026.10.07-mail)。

发布目录：`release/CAN-WIFI_20261007_MAIL_Win64/`。
压缩包：`release/CAN-WIFI_20261007_MAIL_Win64.zip`。

适用 Windows 10/11 64位与 ESP32-S3 N16R8（16MB Flash、8MB八线 PSRAM）。
EXE 内嵌 Python 运行时、esptool 4.12.0、Tk 图形界面及本版固件，无需预装 Python、ESP-IDF 或 Node.js，烧录可完全离线。
需要电脑正常识别 USB 串口；板载原生 USB-Serial/JTAG 优先，USB 转 UART 口可能需要相应芯片驱动。

## 使用

1. 解压整个 ZIP，连接开发板的 USB 数据口，关闭占用串口的软件。
2. 双击 `CAN-WIFI-Flasher.exe`，选择实际串口（当前设备为 COM8）。
3. 点击“开始烧录”，等待“烧录成功”；设备会自动重启。
4. 使用设备保存的网络配置访问网页；启动时 PSRAM 测试可能需要数秒。

默认烧录速度 460800；通信不稳定可选择 115200。
连接失败可按住 BOOT、点按 RESET 后重试。
EXE 内包含全部固件，仅复制这个 EXE 到其他目录也能烧录。

固件 SHA-256 在烧录前校验，esptool 写入后校验设备内容；失败会在界面日志显示，成功后才显示完成。

## 乐鑫官方网页版烧录

也可使用 Chrome 或 Edge 打开 [乐鑫 ESP Tool](https://espressif.github.io/esptool-js/)，直接选择本包中的三个 BIN 文件。
浏览器须支持 Web Serial，并且电脑已经识别开发板 USB 串口，无需安装 Python 或 ESP-IDF。

1. 解压烧录包，连接 USB 数据线，关闭占用开发板串口的软件。
2. 在网页 **Program** 区域将 **Baudrate** 设为 **460800**，点击 **Connect**。
3. 在浏览器弹窗选择开发板串口（本机 COM8），确认网页识别为 **ESP32-S3**。
4. 使用 **Add File** 添加三行，在 **Flash Address** 中填写以下十六进制地址，并选择对应文件：

   | Flash Address | 本包文件 |
   |---|---|
   | `0x0` | `firmware/bootloader.bin` |
   | `0x8000` | `firmware/partition-table.bin` |
   | `0x10000` | `firmware/can_monitor.bin` |

5. 设置 **Flash Mode = dio**、**Flash Frequency = 80MHz**、**Flash Size = 16MB**，点击 **Program** 开始烧录。
6. 等待全部文件写入完成、日志没有错误后，点击 **Disconnect**，按开发板 **RESET** 重启。

**保留已有配置时不要点击 Erase Flash。** 本项目兼容布局下，分别写入上述三段文件会保留 NVS 中的 WiFi、邮箱授权和曲线配置。
网页默认显示的地址需要改为本表地址，尤其 ESP32-S3 的 bootloader 从 `0x0` 开始。
不上传 ZIP、EXE、ELF 或 manifest.json，三个文件均选择 `.bin`。
连接失败可按住 BOOT、点按 RESET 后松开 BOOT，再重新 Connect；通信不稳定可降到 115200 重试。
首次打开网页需加载在线资源，准备完全离线烧录时使用本包 EXE。

以上按钮和参数依据 [乐鑫官方工具及源码](https://github.com/espressif/esptool-js) 核对；本版本已验证 EXE 实机烧录，未重复执行网页版实机烧录。

## 配置保留与邮箱授权

工具按三个地址分别写入，参数 DIO / 80MHz / 16MB：

| 地址 | 文件 |
|---|---|
| `0x0` | bootloader.bin |
| `0x8000` | partition-table.bin |
| `0x10000` | can_monitor.bin |

本项目 NVS 的 `0x9000～0xEFFF` 不在写入区域，兼容布局升级保留 WiFi、收件地址、邮箱授权和曲线信号配置。
浏览器 Record 数据不属于 NVS，需要时先导出记录再烧录。
从其他项目迁移时应核对分区布局；本说明的配置保留适用于本项目兼容布局。

本包不包含用户的 OAuth 凭据或设备 NVS。已有设备保留授权，新板需首次授权与串口 `mailauth` 导入，见 [串口协议](serial-protocol.md)。
发件邮箱 `espdata@agent.qq.com`，默认收件邮箱 `lichen1435374410@163.com`。
CAN TX=GPIO5、RX=GPIO4、250kbps；支持 CSV 信封发送和串口 `status` 邮箱字段。

不使用整片擦除，也不合并三段为包含填充区的原始 BIN；填充区从 0 地址写入会覆盖 NVS。
`firmware` 目录提供原始文件、大小及 SHA-256 清单，供其他烧录器按表中地址使用。
相关机制参见 [esptool 官方烧录说明](https://docs.espressif.com/projects/esptool/en/release-v4/esp32/esptool/basic-commands.html)。

## 包内容

- `CAN-WIFI-Flasher.exe`：图形烧录器，内嵌运行环境和固件。
- `烧录说明.txt`：接线、使用、默认值与故障处理。
- `firmware/`：三段 BIN 与 manifest.json。
- `docs/`：串口协议、正弦 CAN 测试说明、本说明。
- `tools/`：正弦 CAN 测试和首次邮箱授权导入脚本，额外操作需安装 Python，独立 EXE 烧录不需要。
- `SHA256SUMS.txt`：包内文件 SHA-256 清单。
- `source.zip`、`licenses/`、`THIRD-PARTY-NOTICES.txt`：烧录器及 esptool 源码、资源与许可。

## 可选命令行诊断

窗口程序通过 `--report` 将结果保存到 UTF-8 文件，退出码 0 表示成功：

```powershell
.\CAN-WIFI-Flasher.exe --verify-package --report check.json
.\CAN-WIFI-Flasher.exe --list-ports --report ports.txt
.\CAN-WIFI-Flasher.exe --flash COM8 --baud 460800 --report flash.log
```

`--verify-package` 和 `--list-ports` 不连接或复位设备。

## 开发者重新打包

最终用户使用 EXE 无需开发环境。重新构建 EXE 的开发者需要 ESP-IDF 5.3.5、Python 3.11 64位、PyInstaller，
以及该 IDF 环境中的 esptool 4.12.0。先构建当前固件：

```powershell
. C:/Espressif/tools/Microsoft.v5.3.5.PowerShell_profile.ps1
idf.py -B build-mail -D SDKCONFIG=build-mail/sdkconfig build
```

再在安装了 PyInstaller 的 Python 环境调用打包脚本。当前机器的命令如下（site-packages 路径随安装位置变化）：

```powershell
$env:PYTHONPATH='C:/Espressif/tools/python/v5.3.5/venv/Lib/site-packages'
python tools/package-offline-firmware.py
```

脚本核对 `flasher_args.json` 的地址和参数、生成固件清单，然后将固件和 esptool 资源打进单文件 EXE。
仅选入发布固件、说明和开源工具源码，不读取注册表 OAuth 凭据或设备 NVS。
其他构建目录可用 `--build-dir` 指定。重新发布前应验证内嵌校验、图形界面初始化和实际烧录，并确认配置保留。

## 已执行验证（2026-10-07）

- 单独复制 EXE 到含中文和空格的目录，不提供外部 firmware 目录；清除 Python、ESP-IDF 环境变量，PATH 仅保留 Windows 系统目录。
- 内嵌固件 SHA-256 校验、Tk 界面初始化、串口枚举及无效串口拒绝均通过。
- 使用该独立 EXE 在 COM8 烧录，三段固件全部通过设备写入校验，重启联网成功。
- WiFi、收件地址、授权配置状态、四路曲线信号定义均保留；CAN RUNNING，TEC=0、REC=0。

该验证证明 EXE 无需本机开发工具路径或外部 Python 模块；尚未在另一台全新 Windows 电脑上验证 USB 驱动识别。
