# 四路曲线正弦 CAN 测试

脚本：[tools/can-sine-test.py](../tools/can-sine-test.py)。通过电脑连接的 PCAN-USB 向 ESP32-S3 发送经典 CAN 扩展帧，
四个物理量持续按正弦变化，匹配网页预置的 Torque / Speed / Current / Voltage 曲线。

## 接线和运行

将 PCAN-USB 的 CAN_H、CAN_L 接到设备 CAN 收发器的对应端，按适配器接线说明接信号地，确保总线两端各有 120Ω 终端电阻。
ESP32-S3 当前 GPIO5 为 TX、GPIO4 为 RX，两者连接收发器逻辑侧，不能直接连接 PCAN 的 CAN_H/CAN_L。
设备收发器 VIO 接 3.3V。两端波特率均为 **250kbps**。

Windows 需要安装 PEAK 的 PCAN-USB 驱动及匹配 Python 位数的 PCAN-Basic 库。
接口配置见 [python-can 官方 PCAN 文档](https://python-can.readthedocs.io/en/stable/interfaces/pcan.html)。
本脚本用 python-can 4.6.1 验证；推荐 Python 3.10 或更新版本。

在项目根目录执行：

```powershell
python -m pip install --index-url https://pypi.org/simple -r tools/requirements-can-test.txt
python tools/can-sine-test.py
```

默认接口 `pcan`、通道 `PCAN_USBBUS1`、250000bit/s；持续循环发送，**Ctrl+C 停止并释放适配器**。
不要让 PCAN-View 等其他程序以不同波特率占用同一通道。
也可指定时长或另一通道：

```powershell
python tools/can-sine-test.py --duration 60
python tools/can-sine-test.py --channel PCAN_USBBUS2
```

不连接硬件，只查看生成的报文和波形值（无需安装 python-can）：

```powershell
python tools/can-sine-test.py --dry-run --duration 3
```

## 默认波形和编码

`t` 为脚本开始发送后的秒数，物理量按 `偏置 + 幅度 × sin(2πt/周期 + 相位)` 生成。
每个信号使用不同周期和相位，便于识别四条曲线。

| 曲线 | 默认波形 | 范围 | 周期 | 相位 |
|---|---|---|---|---|
| Torque 转矩 | `0 + 1000 × sin(...)` Nm | −1000～1000 Nm | 6s | 0° |
| Speed 转速 | `0 + 6000 × sin(...)` rpm | −6000～6000 rpm | 8s | 90° |
| Current 电流 | `200 + 100 × sin(...)` A | 100～300 A | 10s | 180° |
| Voltage 电压 | `400 + 40 × sin(...)` V | 360～440 V | 12s | 270° |

| 扩展 CAN ID | 发送间隔 | DLC | 字节与原始值 |
|---|---|---|---|
| `0x18FF0182` | 10ms，目标 100帧/s | 8 | byte1～2=`round(Torque+3000)`；byte3～4=`round(Speed+15000)` |
| `0x18FF0282` | 50ms，目标 20帧/s | 8 | byte0～1=`round((Current+1000)×10)`；byte2～3=`round(Voltage×10)` |

默认均为 **Intel 小端**；未使用字节及故障字段为 0。
编码后转矩/转速分辨率为 1，电流/电压为 0.1。发送的是 CAN 扩展数据帧，非 CAN FD 或远程帧。
电流默认保持正值，避开设备端将 raw=`0x2710`（即 0A）解释为电流零漂故障的哨兵。
自定义电流波形跨过 0A 时，脚本会提示；网页通用曲线解码器仍按普通数值解码。

脚本使用单调时钟计算波形与发送时刻；调度延迟时跳过已经错过的时隙，避免积压后集中补发。
每秒输出累计发送数、跳过时隙数和当前理论物理量，首帧输出 ID 和原始字节。
发送次数表示驱动接受的帧数，不等于 ESP32 已接收；实际接收通过设备页面或 `status.can_total` 核对。
PCAN 总线错误或发送失败会报错并非零退出，退出时释放总线。

## 参数示例

统一四路周期为 5 秒：

```powershell
python tools/can-sine-test.py --period 5
```

单独改变幅度、偏置、周期、相位：

```powershell
python tools/can-sine-test.py --torque-amplitude 500 --speed-period 10 --current-offset 300 --voltage-phase 90
```

每路均支持 `--<名称>-amplitude`、`--<名称>-offset`、`--<名称>-period`、`--<名称>-phase`，
名称为 `torque` / `speed` / `current` / `voltage`，相位单位为度。
每路周期参数优先于统一 `--period`。

修改发送间隔：

```powershell
python tools/can-sine-test.py --motor-interval-ms 20 --bus-interval-ms 100
```

间隔至少 1ms，周期至少为对应报文两个发送间隔，幅度非负，所有数字必须有限。
偏置±幅度必须处于协议范围：转矩 −3000～3000Nm、转速 −15000～15000rpm、电流 −1000～1000A、电压 0～1000V。
脚本拒绝越界配置，不自动裁剪。
`--byte-order big` 仅用于同步改成大端的解码配置；当前网页和设备都采用默认小端。
完整选项：`python tools/can-sine-test.py --help`。

Linux SocketCAN 示例（先将 `can0` 配置到 250000bit/s 并启用）：

```bash
python tools/can-sine-test.py --interface socketcan --channel can0
```

## 在网页检查曲线

1. 打开设备网页，确认 CAN Monitor 收到 `0x18FF0182` 和 `0x18FF0282`，DLC=8、Ext=true。
2. 切换到“自定义曲线”，确认四个预置信号已启用且配置与上述表格一致。
3. 选择 30s 或 60s 窗口，可观察各路完整周期。曲线数据从网页打开后开始积累。
4. 点击 Record，记录一段后 Stop；可导出 CSV，或点击信封发送该记录。

脚本不修改设备信号配置，不清空消息、不启动或停止录制，也不发送邮件。
本机 ESP32 的 HTTP `/api/send` 是向 CAN 总线发送的接口，当前 NORMAL 模式不会把自己的发送自动当成接收数据，
因此曲线接收测试使用外接 PCAN-USB。

遇到初始化失败，检查驱动、PCAN-Basic 库、通道和是否被其他程序占用。
遇到总线错误或设备没有接收，检查 250kbps、CAN_H/CAN_L、信号地、终端电阻及收发器供电；
串口 `status` 中 `twai` 应为 `RUNNING`，`tec` / `rec` 可辅助定位错误。

## 已执行验证（2026-10-07）

- 独立按网页解码公式核对 2401 个时间点的四路数值、字节位置和量化误差；大小端编码均通过。
- 非有限数值、越界幅度、无效周期和间隔拒绝执行；发送失败及 Ctrl+C 均释放总线。
- python-can 4.6.1 虚拟总线确认两路扩展 DLC8 报文及数值变化。
- PCAN-USB → ESP32-S3 实机发送 13 秒：电机 1300 帧、母线 260 帧，跳过时隙 0/0；
  设备 `can_total` 增加 1560，全部接收。解码范围分别为 −1000～1000Nm、−6000～6000rpm、100～300A、360～440V。

验证未清空原有设备数据，未修改信号配置，未发送邮件；实机短时测试结束后停止发送并释放 PCAN。
