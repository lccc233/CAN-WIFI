#!/usr/bin/env python3
"""Fetch and independently verify a stopped real-device stress recording and browser CSV."""
import argparse
from collections import Counter
import csv
import hashlib
import json
import math
from pathlib import Path
import secrets
import urllib.request


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def json_lines(path):
    with path.open(encoding="utf-8") as source:
        return [json.loads(line) for line in source if line.strip()]


def request(opener, base, path, body=None):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(base + path, data=data,
        headers={"Content-Type": "application/json"} if body is not None else {})
    with opener.open(req, timeout=15) as response:
        value = json.load(response)
    if value.get("ok") is False:
        raise RuntimeError(value.get("error", "Device request failed"))
    return value


def fetch_record(folder, base):
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    state = request(opener, base, "/api/rec/status")
    rec = state["rec"]
    if rec["on"] or not rec["session"]:
        raise RuntimeError("Recorder must be stopped before final verification")
    client = secrets.token_hex(8)
    total = rec["cnt"]
    offset = 0
    target = folder / "device-frames.jsonl"
    if target.exists():
        raise RuntimeError("Refusing to overwrite downloaded source frames")
    with target.open("x", encoding="utf-8") as output:
        while offset < total:
            value = request(opener, base,
                f"/api/rec/data?session={rec['session']}&client={client}&from={offset}")
            packet = value["frames"]
            if (value["rec"]["session"] != rec["session"] or value["rec"]["on"]
                or value["rec"]["cnt"] != total or value["from"] != offset
                or not 1 <= len(packet) <= 128 or value["next"] != offset + len(packet)):
                raise RuntimeError("Stopped source or continuous cursor changed")
            for index, frame in enumerate(packet):
                if frame["seq"] != offset + index:
                    raise RuntimeError("Non-continuous device frame sequence")
                output.write(json.dumps(frame, ensure_ascii=False) + "\n")
            offset += len(packet)
    ack = request(opener, base, "/api/rec/ack",
        {"session": rec["session"], "client": client, "count": offset})
    if ack.get("session") != rec["session"] or ack.get("count") != total:
        raise RuntimeError("Final source acknowledgment mismatch")
    state["verification_ack"] = ack
    (folder / "record-final.json").write_text(
        json.dumps(state, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"Fetched and confirmed {offset} source frames from {rec['session']}")


def key(frame):
    return int(frame["id"], 16), bytes.fromhex(frame["data"])


def decode(data, signal):
    if signal["endian"] != "intel":
        raise RuntimeError("This independent stress decoder requires Intel test signals")
    raw = (int.from_bytes(data, "little") >> signal["start"]) & ((1 << signal["len"]) - 1)
    if signal["signed"] and raw & (1 << (signal["len"] - 1)):
        raw -= 1 << signal["len"]
    return raw * signal["factor"] + signal["offset"]


def verify(folder, csv_path):
    state = read_json(folder / "record-final.json")
    sent = json_lines(folder / "target-sends.jsonl")
    echoes = json_lines(folder / "target-echoes.jsonl")
    frames = json_lines(folder / "device-frames.jsonl")
    rec = state["rec"]
    checks = {}
    def check(name, condition):
        checks[name] = bool(condition)

    sender = read_json(folder / "summary.json")
    check("sender_completed_successfully", sender["status"] == "completed" and sender["sender_verification_ok"])
    check("five_minute_send_window", sender["duration_s"] >= 300 and sender["configuration"]["duration_s"] == 300)
    check("64_background_ids_at_20_hz_requested", sender["configuration"]["background_ids"] == 64
        and sender["configuration"]["background_hz_per_id"] == 20
        and sender["configuration"]["requested_total_fps"] == 1400)
    check("every_sent_frame_has_tx_confirmation", sender["total_sent"] == sender["total_echo_matched"]
        and sender["pending_count"] == 0 and sender["send_failures"] == 0 and not sender["receiver_errors"]
        and all(item.get("driver_accepted", 0) == item.get("tx_echo_confirmed", 0) for item in sender["by_id"].values()))
    check("all_background_streams_ran", len(sender["background_sent_by_id"]) == 64
        and all(abs(count + sender["skipped"][ident] - 6000) <= 1 for ident, count in sender["background_sent_by_id"].items()))
    check("target_log_counts_match_sender_report", sum(sender["target_sent"].values()) == len(sent))
    check("pcan_bus_status_remained_ok", sender["bus_status_ok"])
    confirm = sender["tx_confirmation"]
    check("no_error_or_unmatched_echo_frames", all(confirm.get(name, 0) == 0 for name in
        ["rx_error_frames", "duplicate_echo", "unmatched_echo", "echo_on_failed_send"]))
    before = read_json(folder / "before.json")
    check("frozen_definitions_match_four_original_signals", state["signals"] == before["signals"]["signals"]
        and len([s for s in state["signals"] if s["enabled"]]) == 4)
    check("only_original_target_ids_recorded", {int(f["id"], 16) for f in frames} == {0x18ff0182, 0x18ff0282})
    hardware_checks = read_json(folder / "hardware-checks.json")
    checks.update({"hardware_" + name: bool(value) for name, value in hardware_checks.items()})
    check("nonempty_stress_record", len(frames) > 0 and len(sent) > 0)
    check("target_send_echo_exact_payloads", Counter(map(key, sent)) == Counter(map(key, echoes)))
    check("all_confirmed_targets_recorded_exactly_once", Counter(map(key, echoes)) == Counter(map(key, frames)))
    check("continuous_device_sequence", all(f["seq"] == i for i, f in enumerate(frames)))
    check("record_count_matches_sources", len(frames) == rec["cnt"] == len(sent) == len(echoes))
    check("all_target_frames_extended_dlc8", all(f["ext"] and f["dlc"] == 8 for f in frames))
    check("device_reports_no_record_or_receive_loss", rec["drop"] == 0 and rec["rx_lost"] == 0 and rec["quality_known"])
    check("device_record_not_full", not rec["on"] and not rec["full"])
    per_id = {}
    for ident in sorted({int(s["id"], 16) for s in sent}):
        expected = [key(s)[1] for s in sent if key(s)[0] == ident]
        actual = [key(f)[1] for f in frames if key(f)[0] == ident]
        check(f"0x{ident:08x}_payload_order", expected == actual)
        per_id[f"0x{ident:08x}"] = {"sent": len(expected), "recorded": len(actual)}
    check("record_order_matches_echo_order", list(map(key, frames)) == list(map(key, echoes)))
    sigs = [s for s in state["signals"] if s["enabled"]]
    stats = {s["name"]: {"min": math.inf, "max": -math.inf, "samples": 0} for s in sigs}
    mismatch = None
    csv_count = 0
    with csv_path.open(encoding="utf-8-sig", newline="") as source:
        reader = csv.reader(source)
        header = next(reader)
        expected_header = ["no", "time_rel_ms"] + [s["name"] + ("(" + s["unit"] + ")" if s["unit"] else "") + "@" + s["id"] for s in sigs]
        check("csv_frozen_header", header == expected_header)
        for index, row in enumerate(reader):
            csv_count += 1
            if index >= len(frames) or len(row) != 2 + len(sigs):
                mismatch = mismatch or {"row": index, "reason": "unexpected row/column count"}
                continue
            frame = frames[index]
            if row[0] != str(index) or row[1] != str(frame["t"] - frames[0]["t"]):
                mismatch = mismatch or {"row": index, "reason": "sequence/time"}
            for column, sig in enumerate(sigs, 2):
                if int(sig["id"], 16) != int(frame["id"], 16):
                    if row[column] != "":
                        mismatch = mismatch or {"row": index, "reason": "nonmatching ID cell not empty"}
                    continue
                expected = decode(bytes.fromhex(frame["data"]), sig)
                try:
                    actual = float(row[column])
                    if not math.isfinite(actual) or not math.isclose(actual, expected, rel_tol=1e-12, abs_tol=1e-9):
                        mismatch = mismatch or {"row": index, "signal": sig["name"], "expected": expected, "actual": row[column]}
                except ValueError:
                    mismatch = mismatch or {"row": index, "signal": sig["name"], "reason": "missing/nonnumeric"}
                stat = stats[sig["name"]]
                stat["min"] = min(stat["min"], expected)
                stat["max"] = max(stat["max"], expected)
                stat["samples"] += 1
    check("csv_exact_row_count", csv_count == len(frames))
    check("csv_every_value_time_and_empty_cell_matches_source", mismatch is None)
    for stat in stats.values():
        if not stat["samples"]:
            stat["min"] = stat["max"] = None
    report = {"passed": all(checks.values()), "checks": checks, "record": rec,
        "sender_duration_s": sender["duration_s"], "total_sent": sender["total_sent"],
        "background_sent": sender["background_sent"], "sender_schedule_skips": sender["skipped"],
        "target_counts": per_id, "csv_rows": csv_count, "signal_stats": stats,
        "first_csv_mismatch": mismatch, "csv_sha256": hashlib.sha256(csv_path.read_bytes()).hexdigest()}
    (folder / "verification.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return report["passed"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report-dir", type=Path, required=True)
    parser.add_argument("--base-url", default="http://192.168.101.250")
    parser.add_argument("--fetch", action="store_true")
    parser.add_argument("--csv", type=Path)
    args = parser.parse_args()
    if args.fetch:
        fetch_record(args.report_dir, args.base_url.rstrip("/"))
    if args.csv and not verify(args.report_dir, args.csv):
        raise SystemExit(1)
    if not args.fetch and not args.csv:
        parser.error("Specify --fetch or --csv")


if __name__ == "__main__":
    main()
