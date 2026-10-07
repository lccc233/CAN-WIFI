#!/usr/bin/env python3
"""Send the existing four sine signals plus phased background J1939 traffic.

No HTTP recording/configuration operations are performed. Defaults: PCAN USB1,
250 kbit/s, 300 s, 64 background IDs at 20 Hz each (1400 total frames/s).
Local dependencies are loaded from release/can-test-deps. A dry run never opens
CAN hardware. Actual sends and matched TX echoes are recorded separately.
"""
from __future__ import annotations

from collections import Counter, deque
import importlib.util
import heapq
import json
import math
import os
from pathlib import Path
import sys
import threading
import time
from datetime import datetime, timezone


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("can_sine_test", ROOT / "tools/can-sine-test.py")
sine = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = sine
spec.loader.exec_module(sine)


def atomic_json(path: Path, value: dict) -> None:
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("w", encoding="utf-8") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def write_line(stream, value: dict) -> None:
    stream.write(json.dumps(value, ensure_ascii=False, separators=(",", ":"), allow_nan=False) + "\n")


def make_payload(index: int, slot: int, elapsed: float, waves: dict, byte_order: str) -> bytes:
    serial = (slot & 0xFFFFFFFF).to_bytes(4, "little")
    if index < 2:
        payload = bytearray(sine.encode_frames(sine.signal_values(elapsed, waves), byte_order)[index])
        if index == 0:
            # These bytes are outside the four recorded sine signal fields.
            # Fault-related bytes carry a test sequence, not a physical fault.
            payload[0] = serial[0]
            payload[5:8] = serial[1:4]
        else:
            payload[4:8] = serial
        return bytes(payload)
    return serial + (index - 2).to_bytes(2, "little") + b"\x5a\xa5"


def main(argv=None) -> int:
    parser = sine.make_parser()
    parser.description = __doc__
    parser.set_defaults(duration=300.0)
    parser.add_argument("--background-ids", type=int, default=64, help="背景 ID 数量，默认64，最大64")
    parser.add_argument("--background-hz", type=sine.finite_float, default=20.0, help="每个背景ID帧率，默认20Hz")
    parser.add_argument("--report-dir", type=Path, default=None, help="日志目录；保留其他文件，拒绝覆盖本脚本旧日志")
    parser.add_argument("--progress-seconds", type=sine.finite_float, default=10.0)
    parser.add_argument("--echo-drain-seconds", type=sine.finite_float, default=3.0)
    parser.add_argument("--require-tx-echo", action="store_true", help="无法启用PCAN TX echo时禁止发送")
    args = parser.parse_args(argv)
    waves = sine.validate_args(parser, args)
    if not 0 <= args.background_ids <= 64 or args.background_hz <= 0:
        parser.error("背景 ID 数量必须0..64，每ID频率必须大于0")
    if args.progress_seconds <= 0 or args.echo_drain_seconds < 0:
        parser.error("进度周期必须大于0，echo等待时间不能为负")
    report = args.report_dir or ROOT / "build-mail" / ("recording-stress-" + datetime.now().strftime("%Y%m%d-%H%M%S"))
    report = report.resolve()
    report.mkdir(parents=True, exist_ok=True)
    names = ("target-sends.jsonl", "target-echoes.jsonl", "events.jsonl", "progress.json", "summary.json")
    if any((report / name).exists() for name in names):
        parser.error("日志已存在，请使用新的 --report-dir；不会覆盖旧测试证据")
    ids = [sine.MOTOR_ID, sine.BUS_ID] + [(3 << 26) | (0xFF20 + n) << 8 | 0x80 for n in range(args.background_ids)]
    intervals = [args.motor_interval_ms / 1000, args.bus_interval_ms / 1000] + [1 / args.background_hz] * args.background_ids
    phases = [0.0, 0.0] + [n / args.background_ids / args.background_hz for n in range(args.background_ids)]
    counts = [Counter() for _ in ids]
    pending = Counter()  # outstanding (CAN ID, full 8-byte payload), bounded by TX backlog
    pending_meta = {}
    lock = threading.Lock()
    stop_reader = threading.Event()
    reader_errors = []
    extras = Counter()
    recent_echoes = deque(maxlen=4096)
    recent_echo_set = set()
    bus_status = []
    started_utc = datetime.now(timezone.utc).isoformat()
    bus = sleeper = reader = None
    can_module = None
    echo_enabled = False
    echo_reason = "dry_run" if args.dry_run else "not initialized"
    start = time.perf_counter()
    start_ns = time.perf_counter_ns()
    send_end = start
    error = None
    result = "running"
    seq = 0
    streams = [0] * len(ids)
    target_log = (report / names[0]).open("x", encoding="utf-8", buffering=65536)
    echo_log = (report / names[1]).open("x", encoding="utf-8", buffering=65536)
    event_log = (report / names[2]).open("x", encoding="utf-8", buffering=65536)
    configuration = {
        "dry_run": args.dry_run, "interface": args.interface, "channel": args.channel,
        "bitrate": sine.BITRATE, "duration_s": args.duration, "background_ids": args.background_ids,
        "background_hz_per_id": args.background_hz, "requested_total_fps": sum(1 / x for x in intervals),
        "background_priority": 3, "background_source_address": "0x80", "background_pgn_range": "0xFF20..0xFF5F",
        "target_sequence_bytes": {"0x18FF0182": [0, 5, 6, 7], "0x18FF0282": [4, 5, 6, 7]},
        "target_sequence_byte_order": "little", "signal_byte_order": args.byte_order,
        "waves": {name: vars(wave) for name, wave in waves.items()},
        "note": "Motor fault bytes carry test sequence; four sine fields preserve existing encoding. Schedule skips are unsent slots, not device receive loss."
    }

    def snapshot(final=False):
        with lock:
            by_id = {f"0x{can_id:08X}": dict(counts[i]) for i, can_id in enumerate(ids)}
            outstanding = sum(pending.values())
            extra = dict(extras)
            failures = list(reader_errors)
        target = Counter()
        background = Counter()
        for i, item in enumerate(by_id.values()):
            (target if i < 2 else background).update(item)
        return {
            "schema": 1, "status": result, "final": final, "started_utc": started_utc,
            "elapsed_s": time.perf_counter() - start, "send_elapsed_s": send_end - start if final else time.perf_counter() - start,
            "configuration": configuration, "tx_confirmation": {"enabled": echo_enabled, "reason": echo_reason,
                "pending": outstanding, "reader_errors": failures, **extra},
            "target": dict(target), "background": dict(background), "by_id": by_id,
            "bus_status": list(bus_status), "error": error,
            "schedule_skips_meaning": "Unsent scheduling slots, never receiver loss",
            "queue_success_meaning": "Driver accepted send; physical transmission requires matched TX echo",
            "verification": "simulation_only" if args.dry_run else "matched_pcan_tx_echo" if echo_enabled else "driver_queue_only",
            "duration_s": send_end - start if final else time.perf_counter() - start,
            "target_sent": {f"0x{ids[i]:08X}": counts[i]["driver_accepted"] for i in range(2)},
            "background_sent": background["driver_accepted"],
            "background_sent_by_id": {f"0x{ids[i]:08X}": counts[i]["driver_accepted"] for i in range(2, len(ids))},
            "total_sent": target["driver_accepted"] + background["driver_accepted"],
            "total_echo_matched": target["tx_echo_confirmed"] + background["tx_echo_confirmed"],
            "pending_count": outstanding,
            "send_failures": target["send_failed"] + background["send_failed"],
            "receiver_errors": failures,
            "bus_status_ok": None if args.dry_run else bool(bus_status) and all(x["code"] == 0 for x in bus_status),
            "skipped": {f"0x{ids[i]:08X}": counts[i]["schedule_skipped"] for i in range(len(ids))},
        }

    def receive_loop():
        while not stop_reader.is_set():
            try:
                message = bus.recv(timeout=0.02)
                if message is None:
                    continue
                now_ns = time.perf_counter_ns()
                if message.is_error_frame:
                    with lock:
                        extras["rx_error_frames"] += 1
                    continue
                if message.is_rx:
                    with lock:
                        extras["non_echo_rx"] += 1
                    continue
                key = (message.arbitration_id, bytes(message.data))
                with lock:
                    if pending[key] > 0:
                        pending[key] -= 1
                        if not pending[key]:
                            del pending[key]
                        meta = pending_meta.pop(key)
                        meta["echoed"] = True
                        counts[meta["index"]]["tx_echo_confirmed"] += 1
                        if len(recent_echoes) == recent_echoes.maxlen:
                            recent_echo_set.discard(recent_echoes[0])
                        recent_echoes.append(key)
                        recent_echo_set.add(key)
                    else:
                        extras["duplicate_echo" if key in recent_echo_set else "unmatched_echo"] += 1
                        meta = None
                if meta is not None and meta["index"] < 2:
                    write_line(echo_log, {
                        "event": "target_tx_echo", "seq": meta["seq"], "stream_seq": meta["stream_seq"],
                        "id": f"0x{key[0]:08X}", "data": key[1].hex(" ").upper(), "dlc": len(key[1]),
                        "echo_monotonic_ns": now_ns, "elapsed_s": (now_ns - start_ns) / 1e9,
                        "pcan_timestamp": message.timestamp,
                        "echo_latency_ms": (now_ns - meta["monotonic_ns"]) / 1e6,
                    })
            except Exception as exc:
                with lock:
                    reader_errors.append(str(exc))
                return

    def inspect_bus():
        if bus is None:
            return
        code = bus.status() if callable(getattr(bus, "status", None)) else None
        code = getattr(code, "value", code)
        status_string = bus.status_string() if callable(getattr(bus, "status_string", None)) else None
        item = {"elapsed_s": time.perf_counter() - start, "code": code, "description": status_string}
        bus_status.append(item)
        write_line(event_log, {"event": "bus_status", **item})
        sine.check_bus_status(bus)

    try:
        if not args.dry_run:
            dependencies = ROOT / "release/can-test-deps"
            if dependencies.is_dir():
                sys.path.insert(0, str(dependencies))
            import can as can_module
            # Root preflight verified receive_own_messages on this PCAN channel.
            # Unsupported echo fails initialization rather than silently dropping
            # to a driver-queue-only result when require-tx-echo is requested.
            bus = can_module.Bus(interface=args.interface, channel=args.channel,
                bitrate=sine.BITRATE, receive_own_messages=True, ignore_config=True)
            echo_enabled = args.interface.lower() == "pcan"
            echo_reason = "PCAN receive_own_messages enabled; is_rx=False matched by ID and all 8 bytes" if echo_enabled else "Non-PCAN self reception is not claimed as physical TX confirmation"
            if args.require_tx_echo and not echo_enabled:
                raise RuntimeError("--require-tx-echo requires the PCAN interface")
            inspect_bus()
        sleeper = sine.DeadlineSleeper()
        start = time.perf_counter()
        start_ns = time.perf_counter_ns()
        send_end = start
        deadline_end = start + args.duration if args.duration else math.inf
        schedule = [(start + phases[i], i) for i in range(len(ids))]
        heapq.heapify(schedule)
        progress_deadline = start + args.progress_seconds
        status_deadline = start + 1.0
        if bus is not None:
            reader = threading.Thread(target=receive_loop, name="pcan-tx-echo", daemon=True)
            reader.start()
        atomic_json(report / "progress.json", snapshot())
        print(f"{'DRY RUN' if args.dry_run else 'SEND'} {sum(1 / x for x in intervals):g} fps, {len(ids)} IDs, {args.duration:g}s -> {report}", flush=True)
        print("TX echo: " + echo_reason, flush=True)
        while time.perf_counter() < deadline_end:
            with lock:
                if reader_errors:
                    raise RuntimeError("TX echo reader failed: " + reader_errors[-1])
            deadline, index = schedule[0]
            now = time.perf_counter()
            if now >= deadline:
                heapq.heappop(schedule)
                elapsed = now - start
                payload = make_payload(index, streams[index], elapsed, waves, args.byte_order)
                stamp_ns = time.perf_counter_ns()
                entry = {"seq": seq, "stream_seq": streams[index], "index": index,
                    "monotonic_ns": stamp_ns, "echoed": False}
                seq += 1
                key = (ids[index], payload)
                with lock:
                    counts[index]["attempted"] += 1
                    if bus is not None:
                        pending[key] += 1
                        pending_meta[key] = entry
                accepted = False
                if bus is not None:
                    try:
                        bus.send(can_module.Message(arbitration_id=ids[index], is_extended_id=True,
                            data=payload, check=True), timeout=args.send_timeout)
                        accepted = True
                        with lock:
                            counts[index]["driver_accepted"] += 1
                    except Exception as exc:
                        with lock:
                            counts[index]["send_failed"] += 1
                            if key in pending:
                                pending[key] -= 1
                                if not pending[key]:
                                    del pending[key]
                                pending_meta.pop(key, None)
                            if entry["echoed"]:
                                extras["echo_on_failed_send"] += 1
                        write_line(event_log, {"event": "send_failure", "seq": entry["seq"],
                            "stream_seq": streams[index], "id": f"0x{ids[index]:08X}", "error": str(exc),
                            "monotonic_ns": time.perf_counter_ns()})
                        inspect_bus()
                else:
                    with lock:
                        counts[index]["generated"] += 1
                if index < 2 and (accepted or args.dry_run):
                    write_line(target_log, {"event": "target_generated" if args.dry_run else "target_send",
                        "seq": entry["seq"], "stream_seq": streams[index], "id": f"0x{ids[index]:08X}",
                        "data": payload.hex(" ").upper(), "dlc": 8, "extended": True,
                        "monotonic_ns": stamp_ns, "send_return_monotonic_ns": time.perf_counter_ns(),
                        "elapsed_s": (stamp_ns - start_ns) / 1e9, "scheduled_elapsed_s": deadline - start,
                        "accepted_by_driver": accepted, "dry_run": args.dry_run})
                next_deadline, skipped = sine.advance_deadline(deadline, intervals[index], time.perf_counter())
                with lock:
                    counts[index]["schedule_skipped"] += skipped
                streams[index] += 1 + skipped
                heapq.heappush(schedule, (next_deadline, index))
            now = time.perf_counter()
            if now >= status_deadline:
                inspect_bus()
                status_deadline, _ = sine.advance_deadline(status_deadline, 1.0, now)
            if now >= progress_deadline:
                target_log.flush()
                atomic_json(report / "progress.json", snapshot())
                state = snapshot()
                print(f"t={state['elapsed_s']:.1f}s target={state['target']} background={state['background']} pending_echo={state['tx_confirmation']['pending']}", flush=True)
                progress_deadline, _ = sine.advance_deadline(progress_deadline, args.progress_seconds, now)
            delay = min(schedule[0][0], status_deadline, progress_deadline, deadline_end) - time.perf_counter()
            if delay > 0:
                sleeper.sleep(min(delay, 0.1))
        send_end = time.perf_counter()
        result = "completed"
    except KeyboardInterrupt:
        send_end = time.perf_counter()
        result = "interrupted"
        error = "Ctrl+C"
    except Exception as exc:
        send_end = time.perf_counter()
        result = "failed"
        error = str(exc)
        print("ERROR: " + error, file=sys.stderr, flush=True)
    finally:
        if bus is not None and reader is not None:
            drain_end = time.perf_counter() + args.echo_drain_seconds
            try:
                while time.perf_counter() < drain_end:
                    with lock:
                        remaining = sum(pending.values())
                        receiver_failed = bool(reader_errors)
                    if not remaining or receiver_failed:
                        break
                    time.sleep(0.01)
                inspect_bus()
            except Exception as exc:
                result = "failed"
                error = (error + "; " if error else "") + str(exc)
            stop_reader.set()
            reader.join(timeout=2.0)
            if reader.is_alive():
                result = "failed"
                error = (error + "; " if error else "") + "echo reader did not stop"
        if bus is not None:
            try:
                bus.shutdown()
            except Exception as exc:
                result = "failed"
                error = (error + "; " if error else "") + "shutdown: " + str(exc)
        if sleeper is not None:
            sleeper.close()
        for stream in (target_log, echo_log, event_log):
            stream.flush()
            os.fsync(stream.fileno())
            stream.close()
        state = snapshot(final=True)
        state["sender_verification_ok"] = (result == "completed" and not reader_errors
            and not any(x["send_failed"] for x in counts)
            and (args.dry_run or (echo_enabled and state["tx_confirmation"]["pending"] == 0
                and not extras["echo_on_failed_send"])))
        atomic_json(report / "progress.json", state)
        atomic_json(report / "summary.json", state)
        print(json.dumps({"status": result, "sender_verification_ok": state["sender_verification_ok"],
            "target": state["target"], "background": state["background"],
            "pending_echo": state["tx_confirmation"]["pending"], "summary": str(report / "summary.json")}, ensure_ascii=False), flush=True)
    return 0 if state["sender_verification_ok"] else 130 if result == "interrupted" else 1


if __name__ == "__main__":
    sys.exit(main())
