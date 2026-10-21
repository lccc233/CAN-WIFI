"""Bounded PCAN bench adapter for battery/RCU tests; JSON frames on stdin.

This sends synthetic status broadcasts on an isolated PCAN/ESP32 test bench.
An empty frame list only listens. Never run against a vehicle bus.
"""
import argparse
from collections import Counter
import json
import sys
import time
import can

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--channel', default='PCAN_USBBUS1')
p.add_argument('--duration', type=float, default=6)
args = p.parse_args()
frames = json.loads(sys.stdin.readline())
counts, received = Counter(), []
bus = can.Bus(interface='pcan', channel=args.channel, bitrate=250000,
              receive_own_messages=False, ignore_config=True)
try:
    messages = [(can.Message(arbitration_id=int(f['id'], 16),
                             is_extended_id=f.get('ext', True),
                             data=bytes.fromhex(f['data']), check=True),
                 f.get('period_ms', 500) / 1000) for f in frames]
    start = time.monotonic()
    deadlines = [start] * len(messages)
    print(json.dumps({'ready': True, 'channel': args.channel}), flush=True)
    while time.monotonic() - start < args.duration:
        now = time.monotonic()
        for i, (message, period) in enumerate(messages):
            if now >= deadlines[i]:
                bus.send(message, timeout=.2)
                counts[f'0x{message.arbitration_id:X}'] += 1
                deadlines[i] = now + period
        msg = bus.recv(timeout=.005)
        if msg is not None and msg.is_rx:
            received.append({'id': f'0x{msg.arbitration_id:X}', 'ext': msg.is_extended_id,
                             'rtr': msg.is_remote_frame, 'data': msg.data.hex(' ').upper()})
    print(json.dumps({'sent': dict(counts), 'received': received,
                      'adapter_status': int(bus.status())}), flush=True)
finally:
    bus.shutdown()
