#!/usr/bin/env python3
"""Inspect active CAN traffic and safety status via Control Toolkit API."""

import argparse
import json
import sys
import time
import urllib.request
import urllib.error

DEFAULT_URL = "http://127.0.0.1:8001/api/v1"

def get_json(base_url: str, path: str):
    url = f"{base_url}{path}"
    try:
        with urllib.request.urlopen(url, timeout=3) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.URLError as e:
        print(f"Error connecting to backend at {url}: {e}", file=sys.stderr)
        return None

def print_status(base_url: str):
    st = get_json(base_url, "/status")
    if not st:
        return
    adapter = st.get("adapter", {})
    channels = adapter.get("channels", {})
    estop = st.get("estop", {})
    nodes = estop.get("nodes", {})

    print("=" * 80)
    print(f"CONTROL TOOLKIT STATUS: {st.get('service')} (v{st.get('version')})")
    print(f"Adapter: {adapter.get('identity')} | Health: {adapter.get('health')}")
    print(f"Channels: High={channels.get('high', {}).get('activity')} (rx={channels.get('high', {}).get('rx_count')}) | "
          f"Low={channels.get('low', {}).get('activity')} (rx={channels.get('low', {}).get('rx_count')})")
    print(f"ESTOP Active: {estop.get('active')} | Host Latch: {estop.get('host_latch')}")
    print("Nodes:")
    for name, node in nodes.items():
        print(f"  [{name.upper()}] State={node.get('state')} | Ready={node.get('ready')} | "
              f"EstopActive={node.get('estop_active')} | BlockMask=0x{node.get('block_mask', 0):04x} | "
              f"OutputEnabled={node.get('output_enabled')}")
    print("=" * 80)

def print_messages(base_url: str, show_signals: bool = True):
    state = get_json(base_url, "/state")
    if not state:
        return
    messages = state.get("messages", [])
    print(f"{'BUS':4} {'CAN ID':8} {'KEY':26} {'RATE':10} {'AGE':8} {'STATUS':8}")
    print("-" * 80)
    keys_detailed = {
        "rt:rt_node_status", "sys:sys_node_status", "sys:sys_safety_sts",
        "sys:sys_pwr_cmd", "sys:sys_mode_cmd", "rt:rt_drive_cmd",
        "seb:vcu_seb_req", "rt:brake_diag", "rt:rt_state_rpt"
    }
    for m in messages:
        rate = f"{m.get('observed_rate_hz', 0.0):.1f} Hz"
        age = f"{m.get('age_ms', 0):.0f} ms"
        bus = m.get("bus", "")
        can_id = hex(m.get("can_id", 0))
        key = m.get("key", "")
        fresh = m.get("freshness", "")
        print(f"{bus:4} {can_id:8} {key:26} {rate:10} {age:8} {fresh:8}")
        if show_signals and (key in keys_detailed or m.get("validation_status") != "ok"):
            sigs = {k: v.get("engineering_value") for k, v in m.get("signals", {}).items()}
            print(f"     signals: {sigs}")

def main():
    parser = argparse.ArgumentParser(description="Check CAN bus traffic and node state via Control Toolkit API")
    parser.add_argument("--url", default=DEFAULT_URL, help="Base API URL")
    parser.add_argument("--watch", "-w", type=float, default=0, help="Continuously refresh every N seconds")
    args = parser.parse_args()

    while True:
        print_status(args.url)
        print_messages(args.url)
        if args.watch <= 0:
            break
        time.sleep(args.watch)

if __name__ == "__main__":
    main()
