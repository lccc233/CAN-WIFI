# 五分钟 CAN 高负载实机记录验证

2026-10-08，完整记录新版固件烧录到 ESP32-S3 后，使用 PCAN-USB / PCAN_USBBUS1 / 250kbps 执行五分钟真实 CAN 测试。测试通过：已确认发出的目标帧全部记录，网页 CSV 逐帧核对一致。

## 负载与核对方法

四个正弦信号保持原设置：电机 `0x18FF0182` 每 10ms、母线 `0x18FF0282` 每 50ms。叠加 64 个厂家自定义 J1939 PGN `FF20..FF5F`，优先级 3、源地址 `0x80`，每 ID 20Hz，错开相位均匀发送。共 66 个扩展帧 ID，DLC8，计划总帧率 1400 帧/秒。帧率不是实测总线利用率百分比。

网页同时绘制四路曲线并读取设备记录；另以 HTTP/串口每约 10 秒检查设备。信号配置未改动，记录范围仍为四个信号对应的两个 ID；背景报文用于施加负载。

发送器开启 PCAN TX echo，将发送确认与全部 ID、8 字节载荷匹配。目标报文未参与四路解码的字节携带每 ID 独立序号，区分周期性重复的正弦值；电机故障相关字节因此携带测试序号，不表示真实故障。

停止后网页显示同步完整并导出 CSV。独立检查发送日志、echo、设备原始帧的完整字节、顺序与连续序号；对 CSV 逐行检查 0 基编号、相对时间、四路物理值及其他 ID 的空白列。另核对冻结配置、设备 boot 标识与 CAN 总量基线差。

## 实测结果

实际发送窗口 **300.000139 秒**。

| 对象 | 发送确认 | 设备接收/记录 | 结果 |
| --- | ---: | ---: | --- |
| 全部 CAN 报文 | 419,999 | 419,999 | 一致 |
| 电机目标帧 | 29,999 | 29,999 | 完整 |
| 母线目标帧 | 6,000 | 6,000 | 完整 |
| 背景帧 | 384,000 | 包含在全部接收计数中 | 64 个 ID 各发送 6,000 帧 |
| 目标合计 / CSV 行数 | 35,999 | 35,999 | 逐帧、逐行一致 |

PC 发送调度跳过了 1 个电机时隙，故比理论 36,000 个目标帧少 1 帧；该帧未发到总线。所有实际已发送的目标帧均被记录，不属于设备漏收。无发送失败，全部 echo 已匹配。

- 设备 `rx_lost=0`、`drop=0`，采集质量已知；未录满、未重启，CAN 持续 RUNNING，TEC=0、REC=0。
- 26 次运行观测均无 HTTP 错误或采集丢失；浏览器未报告错误。观测中的 `/api/messages` 响应中位数 203ms、最大 360ms，属于本次观察值。
- Torque/Speed 各 29,999 点，范围分别 −1000..1000Nm、−6000..6000rpm；Current/Voltage 各 6,000 点，范围分别 100..300A、360..440V。
- 全部 31 项核对通过；发送结束并释放 PCAN，设备内本次记录保留。未发送邮件。

本地证据位于 `build-mail/recording-stress-20261008/`：`recording.csv`、`device-frames.jsonl`、`target-sends.jsonl`、`target-echoes.jsonl`、`summary.json`、`verification.json`、`observations.jsonl`、`report.md`、`recording-result.png`。这些为本机测试产物，未包含在发布包中。

## 测试工具

发送器：[can-recording-stress.py](../tools/can-recording-stress.py)。默认运行五分钟，保留现有四路正弦生成器的波形设置；不通过 HTTP 修改配置或启停记录。运行前先在网页开始 Record，发送结束后 Stop 并等待同步完整，保留需要的 CSV。

```powershell
python tools/can-recording-stress.py --duration 300 --report-dir build-mail/new-stress-run --require-tx-echo
```

依赖、驱动与接线见 [正弦测试说明](can-sine-test.md)。优先使用已安装的 python-can，测试机也可使用本地 `release/can-test-deps`；发送器拒绝覆盖既有同名日志。`--dry-run --duration 2` 仅生成数据，不打开 CAN。

核对器：[verify-recording-stress.py](../tools/verify-recording-stress.py)，用于本次既定四路、64 个背景 ID 和五分钟配置。`--fetch` 下载并确认已停止的原始记录；`--csv` 核对真实网页导出的 CSV，需要同目录中的发送/echo 总结、测试前设备快照和硬件核对结果。原始帧下载拒绝覆盖既有证据。

本次结论适用于上述负载和五分钟窗口；邮箱并发、其他负载和更长连续运行需分别验证。
