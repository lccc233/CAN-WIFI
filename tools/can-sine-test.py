#!/usr/bin/env python3
"""经外接 USB-CAN 循环发送曲线页的四路正弦测试数据。

默认 PCAN-USB / PCAN_USBBUS1，250 kbit/s，经典 CAN 扩展帧，DLC=8。
连接 PCAN 的 CAN_H/CAN_L 与设备收发器，按实际总线要求接地与终端电阻。
ESP32 的 GPIO4 是收发器 RX、GPIO5 是 TX，不能直接连接 CAN_H/CAN_L。
本脚本向物理 CAN 总线发送，不调用设备的 HTTP /api/send。

示例：
  python tools/can-sine-test.py --dry-run --duration 3
  python tools/can-sine-test.py --duration 60
  python tools/can-sine-test.py --channel PCAN_USBBUS2 --period 8
  python tools/can-sine-test.py --interface socketcan --channel can0

真实发送需安装 python-can 和适配器驱动：python -m pip install python-can
SocketCAN 的 can0 须事先配置为 250000 bit/s 并启用。
大端仅用于对应的解码配置；当前设备与网页预置都是小端。
"""

import argparse
from dataclasses import dataclass
import math
import sys
import time


MOTOR_ID = 0x18FF0182
BUS_ID = 0x18FF0282
BITRATE = 250_000


@dataclass(frozen=True)
class Wave:
    offset: float
    amplitude: float
    period: float
    phase_deg: float

    def value(self, elapsed: float) -> float:
        # 先取模避免长时间运行或大相位导致三角函数精度下降。
        angle = 2 * math.pi * ((elapsed % self.period) / self.period)
        return self.offset + self.amplitude * math.sin(
            angle + math.radians(self.phase_deg % 360)
        )


@dataclass(frozen=True)
class SignalValues:
    torque: float
    speed: float
    current: float
    voltage: float


# 名称、单位、偏置、幅度、周期、相位、协议物理量上下限。
# 协议范围内电机原始值都小于 32768，兼容网页预置的 signed 16 位解码。
SIGNALS = {
    "torque": ("输出转矩", "Nm", 0.0, 1000.0, 6.0, 0.0, -3000.0, 3000.0),
    "speed": ("当前转速", "rpm", 0.0, 6000.0, 8.0, 90.0, -15000.0, 15000.0),
    "current": ("母线电流", "A", 200.0, 100.0, 10.0, 180.0, -1000.0, 1000.0),
    "voltage": ("母线电压", "V", 400.0, 40.0, 12.0, 270.0, 0.0, 1000.0),
}


def signal_values(elapsed: float, waves: dict[str, Wave]) -> SignalValues:
    """生成某个时间点的四个物理量，供离线检查或其他发送程序复用。"""
    return SignalValues(**{name: waves[name].value(elapsed) for name in SIGNALS})


def encode_frames(values: SignalValues, byte_order: str = "little") -> tuple[bytes, bytes]:
    """按固件协议编码两条 DLC8 报文；量化取最近整数，不裁剪越界数据。"""
    if byte_order not in ("little", "big"):
        raise ValueError("字节序必须是 little 或 big")
    for name, spec in SIGNALS.items():
        value = getattr(values, name)
        if not math.isfinite(value) or not spec[6] <= value <= spec[7]:
            raise ValueError(f"{name} 必须在 {spec[6]:g}..{spec[7]:g} {spec[1]} 内")
    motor = bytearray(8)
    bus = bytearray(8)
    motor[1:3] = round(values.torque + 3000).to_bytes(2, byte_order)
    motor[3:5] = round(values.speed + 15000).to_bytes(2, byte_order)
    bus[0:2] = round((values.current + 1000) * 10).to_bytes(2, byte_order)
    bus[2:4] = round(values.voltage * 10).to_bytes(2, byte_order)
    return bytes(motor), bytes(bus)


def finite_float(text: str) -> float:
    try:
        value = float(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("必须是数值") from exc
    if not math.isfinite(value):
        raise argparse.ArgumentTypeError("必须是有限数值，不能是 NaN 或 Infinity")
    return value


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="通过 PCAN-USB 循环发送转矩、转速、电流、电压四路正弦 CAN 测试数据。",
        epilog=(
            "默认持续发送，Ctrl+C 退出；--dry-run 无需 python-can 或硬件。\n"
            "所有报文为 250 kbit/s 扩展帧、DLC8；默认电机每 10ms、母线每 50ms。\n"
            "PCAN 必须接到设备 CAN 收发器；不要直接连接 ESP32 的 GPIO。\n"
            "需要 python-can 和 PCAN 驱动。大端需同时调整设备与网页解码配置。"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--interface", default="pcan", help="python-can 接口，默认 pcan；也可 socketcan 或 virtual")
    parser.add_argument("--channel", default="PCAN_USBBUS1", help="适配器通道，默认 PCAN_USBBUS1；SocketCAN 使用 can0")
    parser.add_argument("--dry-run", action="store_true", help="只生成报文和统计，不连接硬件或导入 python-can")
    parser.add_argument("--duration", type=finite_float, default=0.0, metavar="秒", help="运行时长，0 为持续发送，默认 0")
    parser.add_argument("--period", type=finite_float, metavar="秒", help="统一四路周期；各信号的 --名称-period 优先")
    parser.add_argument("--motor-interval-ms", type=finite_float, default=10.0, metavar="毫秒", help="电机报文间隔，默认 10，最小 1")
    parser.add_argument("--bus-interval-ms", type=finite_float, default=50.0, metavar="毫秒", help="母线报文间隔，默认 50，最小 1")
    parser.add_argument("--byte-order", choices=("little", "big"), default="little", help="16 位数据字节序，默认 little，与当前解码一致")
    parser.add_argument("--send-timeout", type=finite_float, default=0.1, metavar="秒", help="单帧发送等待上限，默认 0.1，必须大于 0")
    for name, spec in SIGNALS.items():
        group = parser.add_argument_group(f"{spec[0]} ({name})")
        group.add_argument(f"--{name}-offset", type=finite_float, default=spec[2], help=f"偏置，默认 {spec[2]:g} {spec[1]}")
        group.add_argument(f"--{name}-amplitude", type=finite_float, default=spec[3], help=f"幅度，默认 {spec[3]:g} {spec[1]}，必须非负")
        group.add_argument(f"--{name}-period", type=finite_float, metavar="秒", help=f"周期，默认 {spec[4]:g} 秒，至少两个采样间隔")
        group.add_argument(f"--{name}-phase", type=finite_float, default=spec[5], metavar="度", help=f"相位，默认 {spec[5]:g} 度")
    return parser


def validate_args(parser: argparse.ArgumentParser, args: argparse.Namespace) -> dict[str, Wave]:
    if not args.interface.strip() or not args.channel.strip():
        parser.error("--interface 和 --channel 不能为空")
    if args.duration < 0:
        parser.error("--duration 必须大于等于 0")
    if args.send_timeout <= 0:
        parser.error("--send-timeout 必须大于 0")
    if args.period is not None and args.period <= 0:
        parser.error("--period 必须大于 0")
    for option in ("motor_interval_ms", "bus_interval_ms"):
        if getattr(args, option) < 1:
            parser.error(f"--{option.replace('_', '-')} 必须至少为 1 毫秒")
    waves = {}
    for name, spec in SIGNALS.items():
        period = getattr(args, f"{name}_period")
        if period is None:
            period = args.period if args.period is not None else spec[4]
        interval = (args.motor_interval_ms if name in ("torque", "speed") else args.bus_interval_ms) / 1000
        amplitude = getattr(args, f"{name}_amplitude")
        offset = getattr(args, f"{name}_offset")
        if amplitude < 0:
            parser.error(f"--{name}-amplitude 必须非负")
        if period <= 0 or period < 2 * interval:
            parser.error(f"--{name}-period 必须至少为两个采样间隔 ({2 * interval:g} 秒)")
        if offset - amplitude < spec[6] or offset + amplitude > spec[7]:
            parser.error(f"{name} 的 偏置±幅度 必须在 {spec[6]:g}..{spec[7]:g} {spec[1]} 内，不会自动裁剪")
        waves[name] = Wave(offset, amplitude, period, getattr(args, f"{name}_phase"))
    return waves


def advance_deadline(deadline: float, interval: float, now: float) -> tuple[float, int]:
    """保持周期相位，跳过已错过的发送时隙，避免延迟后积压突发。"""
    slots = max(1, math.floor((now - deadline) / interval) + 1)
    next_time = deadline + slots * interval
    if next_time <= now:  # 防止浮点舍入使下个时隙仍然到期。
        next_time += interval
        slots += 1
    return next_time, slots - 1


def check_bus_status(bus) -> None:
    """PCAN 的 send 成功可能只是入队，另外检查驱动报告的总线错误。"""
    status_is_ok = getattr(bus, "status_is_ok", None)
    if callable(status_is_ok) and not status_is_ok():
        status_string = getattr(bus, "status_string", None)
        detail = status_string() if callable(status_string) else "适配器报告非正常状态"
        raise RuntimeError(f"CAN 总线错误：{detail or '状态非正常'}；请检查连接、250 kbit/s、终端电阻及接收节点 ACK")


class DeadlineSleeper:
    """Windows 用高分辨率等待计时器，避免旧版 Python 的 15.6ms sleep。"""

    def __init__(self):
        self.handle = None
        if sys.platform != "win32":
            return
        import ctypes
        from ctypes import wintypes
        self.ctypes = ctypes
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.kernel.CreateWaitableTimerExW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD]
        self.kernel.CreateWaitableTimerExW.restype = wintypes.HANDLE
        self.kernel.SetWaitableTimer.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_longlong), wintypes.LONG, ctypes.c_void_p, ctypes.c_void_p, wintypes.BOOL]
        self.kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        self.kernel.WaitForSingleObject.restype = wintypes.DWORD
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        # CREATE_WAITABLE_TIMER_HIGH_RESOLUTION=2，TIMER_ALL_ACCESS=0x1F0003。
        self.handle = self.kernel.CreateWaitableTimerExW(None, None, 2, 0x1F0003)
        if not self.handle:
            print("提示：系统不支持高分辨率等待计时器，使用普通 sleep，可能跳过发送时隙。", file=sys.stderr)

    def sleep(self, seconds: float) -> None:
        if not self.handle:
            time.sleep(seconds)
            return
        due = self.ctypes.c_longlong(-max(1, round(seconds * 10_000_000)))
        if not self.kernel.SetWaitableTimer(self.handle, self.ctypes.byref(due), 0, None, None, False):
            raise self.ctypes.WinError(self.ctypes.get_last_error())
        if self.kernel.WaitForSingleObject(self.handle, 1000) != 0:
            raise RuntimeError("高分辨率等待计时器等待失败")

    def close(self) -> None:
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None


def run(args: argparse.Namespace, waves: dict[str, Wave]) -> None:
    can_module = None
    bus = None
    sleeper = None
    counts = [0, 0]
    skipped = [0, 0]
    intervals = [args.motor_interval_ms / 1000, args.bus_interval_ms / 1000]
    print(f"{'离线生成' if args.dry_run else '发送'}：{args.interface}/{args.channel}，{BITRATE} bit/s，扩展帧 DLC8，{args.byte_order}", flush=True)
    for name, wave in waves.items():
        print(f"  {SIGNALS[name][0]} = {wave.offset:g} + {wave.amplitude:g} × sin(2πt/{wave.period:g} + {wave.phase_deg:g}°) {SIGNALS[name][1]}")
    current = waves["current"]
    if current.offset - current.amplitude <= 0 <= current.offset + current.amplitude:
        print("提示：电流接近 0A 时可能编码为 0x2710，设备会将它标记为电流零漂故障。", file=sys.stderr)
    print("Ctrl+C 停止。统计中的物理量为当前理论值，编码后转矩/转速精度 1，电流/电压精度 0.1。", flush=True)
    try:
        if not args.dry_run:
            try:
                import can as can_module
            except ImportError as exc:
                raise RuntimeError("缺少 python-can，请运行 python -m pip install python-can；离线可使用 --dry-run") from exc
            bus = can_module.Bus(interface=args.interface, channel=args.channel, bitrate=BITRATE, ignore_config=True)
            check_bus_status(bus)
        sleeper = DeadlineSleeper()
        # perf_counter 也是单调时钟；旧版 Windows 的 monotonic 精度仅约 15.6ms。
        start = time.perf_counter()
        deadlines = [start, start]
        stats_deadline = start + 1.0
        end = start + args.duration if args.duration else math.inf
        while True:
            now = time.perf_counter()
            if now >= end:
                break
            for index, arbitration_id in enumerate((MOTOR_ID, BUS_ID)):
                now = time.perf_counter()
                if now >= end:
                    break
                if now >= deadlines[index]:
                    payload = encode_frames(signal_values(now - start, waves), args.byte_order)[index]
                    if bus is not None:
                        message = can_module.Message(arbitration_id=arbitration_id, is_extended_id=True, data=payload, check=True)
                        try:
                            bus.send(message, timeout=args.send_timeout)
                        except Exception as exc:
                            raise RuntimeError(f"发送 0x{arbitration_id:08X} 失败：{exc}") from exc
                    if counts[index] == 0:
                        print(f"首帧 0x{arbitration_id:08X} [{len(payload)}] {payload.hex(' ').upper()}", flush=True)
                    counts[index] += 1
                    deadlines[index], missed = advance_deadline(deadlines[index], intervals[index], time.perf_counter())
                    skipped[index] += missed
            now = time.perf_counter()
            if now >= stats_deadline:
                if bus is not None:
                    check_bus_status(bus)
                values = signal_values(now - start, waves)
                print(f"t={now - start:7.2f}s 电机={counts[0]} 母线={counts[1]} 跳过时隙={skipped[0]}/{skipped[1]} | 转矩={values.torque:.1f}Nm 转速={values.speed:.1f}rpm 电流={values.current:.1f}A 电压={values.voltage:.1f}V", flush=True)
                stats_deadline, _ = advance_deadline(stats_deadline, 1.0, now)
            delay = min(*deadlines, stats_deadline, end) - time.perf_counter()
            if delay > 0:
                sleeper.sleep(min(delay, 0.1))
        if bus is not None:
            check_bus_status(bus)
    except KeyboardInterrupt:
        print("\n收到 Ctrl+C，停止发送。", flush=True)
    finally:
        try:
            if bus is not None:
                bus.shutdown()
        finally:
            if sleeper is not None:
                sleeper.close()
            print(f"结束：电机 {counts[0]} 帧，母线 {counts[1]} 帧，跳过时隙 {skipped[0]}/{skipped[1]}。", flush=True)


def main(argv: list[str] | None = None) -> int:
    parser = make_parser()
    args = parser.parse_args(argv)
    waves = validate_args(parser, args)
    try:
        run(args, waves)
    except Exception as exc:
        print(f"错误：{exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
