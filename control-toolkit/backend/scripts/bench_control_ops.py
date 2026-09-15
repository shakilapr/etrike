#!/usr/bin/env python3
"""Operations utility for Control Toolkit hardware bench.

Controls session lifecycle, ESTOP reset, power states, operating modes,
and drive commands via REST API.
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.request

DEFAULT_URL = "http://127.0.0.1:8001/api/v1"

def api_request(base_url: str, method: str, path: str, data: dict | None = None) -> dict | None:
    url = f"{base_url}{path}"
    body = json.dumps(data).encode("utf-8") if data is not None else None
    headers = {"Content-Type": "application/json"} if data is not None else {}
    req = urllib.request.Request(url, data=body, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            content = resp.read().decode("utf-8")
            return json.loads(content) if content else {}
    except urllib.error.HTTPError as e:
        err_body = e.read().decode("utf-8", errors="replace")
        print(f"API Error {e.code} on {method} {path}: {err_body}", file=sys.stderr)
        return None
    except urllib.error.URLError as e:
        print(f"Connection error to {url}: {e}", file=sys.stderr)
        return None

def ensure_session_active(base_url: str) -> str | None:
    status = api_request(base_url, "GET", "/status")
    if not status:
        return None
    sess = status.get("session", {})
    session_id = sess.get("session_id")

    if not session_id or sess.get("phase") in ["stopped", "completed", "failed"]:
        print("Creating new bench_test session...")
        res = api_request(base_url, "POST", "/sessions", {"profile": "bench_test"})
        if not res or "session" not in res:
            print("Failed to create session.", file=sys.stderr)
            return None
        sess = res["session"]
        session_id = sess.get("session_id")
        print(f"Session created: id={session_id}")

    if sess.get("bench_tx") != "enabled":
        print(f"Enabling bench TX for session {session_id}...")
        res = api_request(base_url, "POST", f"/sessions/{session_id}/bench-tx", {"enabled": True})
        if res and res.get("session", {}).get("bench_tx") == "enabled":
            print("Bench TX enabled.")
        else:
            print("Failed to enable Bench TX.", file=sys.stderr)
            return None

    return session_id

def inject_message(base_url: str, bus: str, key: str, values: dict, period_ms: float | None = None) -> dict | None:
    payload = {"bus": bus, "key": key, "values": values}
    if period_ms is not None:
        payload["period_ms"] = period_ms
    return api_request(base_url, "POST", "/injections", payload)

def reset_estop(base_url: str) -> bool:
    print("\n[RESET] Executing 2-stage advancing ESTOP reset sequence...")
    if not ensure_session_active(base_url):
        return False

    for ctr in [1, 2, 3]:
        inject_message(base_url, "high", "hmi:host_estop_reset_req", {
            "reset_token": 0x5253,
            "request_seq": ctr,
            "rolling_counter": ctr
        })
        time.sleep(0.02)

    time.sleep(0.2)
    st = api_request(base_url, "GET", "/status")
    if not st:
        return False
    estop_active = st.get("estop", {}).get("active", True)
    nodes = st.get("estop", {}).get("nodes", {})
    print(f"ESTOP active: {estop_active}")
    for name, node in nodes.items():
        print(f"  {name.upper()}: state={node.get('state')} ready={node.get('ready')} block_mask=0x{node.get('block_mask', 0):04x}")
    return not estop_active

def set_power(base_url: str, state_on: bool) -> bool:
    req_val = 1 if state_on else 0
    state_str = "ON" if state_on else "OFF"
    print(f"\n[POWER] Requesting Power {state_str} (hmi:hmi_pwr_req={req_val})...")
    if not ensure_session_active(base_url):
        return False

    for ctr in [1, 2, 3]:
        inject_message(base_url, "high", "hmi:hmi_pwr_req", {
            "req_start": req_val,
            "rolling_counter": ctr
        })
        time.sleep(0.02)

    time.sleep(0.2)
    state = api_request(base_url, "GET", "/state")
    if not state:
        return False
    for m in state.get("messages", []):
        if m.get("key") == "sys:sys_pwr_cmd":
            pwr = m.get("signals", {}).get("power_state", {}).get("engineering_value")
            print(f"SYS_PWR_CMD observed: power_state={pwr}")
            return (pwr == req_val)
    return False

def set_mode(base_url: str, auto_mode: bool) -> bool:
    req_val = 1 if auto_mode else 0
    mode_str = "AUTO" if auto_mode else "MANUAL"
    print(f"\n[MODE] Requesting Mode {mode_str} (hmi:hmi_mode_req={req_val})...")
    if not ensure_session_active(base_url):
        return False

    for ctr in [1, 2, 3]:
        inject_message(base_url, "high", "hmi:hmi_mode_req", {
            "req_mode": req_val,
            "rolling_counter": ctr
        })
        time.sleep(0.02)

    time.sleep(0.2)
    state = api_request(base_url, "GET", "/state")
    if not state:
        return False
    sys_mode = None
    rt_mode = None
    for m in state.get("messages", []):
        if m.get("key") == "sys:sys_mode_cmd":
            sys_mode = m.get("signals", {}).get("mode", {}).get("engineering_value")
        elif m.get("key") == "rt:rt_state_rpt":
            rt_mode = m.get("signals", {}).get("mode", {}).get("engineering_value")
    print(f"Observed Modes: SYS={sys_mode}, RT={rt_mode}")
    return (sys_mode == req_val and rt_mode == req_val)

def stream_drive(base_url: str, speed_mmps: int, yaw_rate_mrad_s: int = 0, duration_s: float = 3.0) -> bool:
    print(f"\n[DRIVE] Streaming speed={speed_mmps} mm/s, yaw={yaw_rate_mrad_s} mrad/s for {duration_s}s...")
    if not ensure_session_active(base_url):
        return False

    inj = inject_message(base_url, "high", "host:host_drive_cmd", {
        "speed_mmps": speed_mmps,
        "yaw_rate_mrad_s": yaw_rate_mrad_s
    }, period_ms=20.0)

    if not inj:
        print("Failed to start drive stream.", file=sys.stderr)
        return False

    job_id = inj.get("job_id")
    print(f"Drive stream active (job_id={job_id}). Observing outputs...")
    start_time = time.time()
    last_print = 0

    while time.time() - start_time < duration_s:
        time.sleep(0.1)
        if time.time() - last_print > 0.5:
            last_print = time.time()
            st = api_request(base_url, "GET", "/state")
            if st:
                for m in st.get("messages", []):
                    if m.get("key") in ["rt:rt_drive_cmd", "seb:vcu_seb_req", "rt:rt_node_status"]:
                        sigs = {k: v.get("engineering_value") for k, v in m.get("signals", {}).items()}
                        print(f"  {m['bus']:4} {m['key']:18}: {sigs}")

    print("\nStopping drive stream...")
    api_request(base_url, "DELETE", "/injections")
    return True

def main():
    parser = argparse.ArgumentParser(description="Control Toolkit hardware bench operations")
    parser.add_argument("--url", default=DEFAULT_URL, help="Base API URL")
    parser.add_argument("--session", action="store_true", help="Ensure session active with Bench TX")
    parser.add_argument("--reset", action="store_true", help="Execute ESTOP reset sequence")
    parser.add_argument("--power", choices=["on", "off"], help="Command vehicle power state")
    parser.add_argument("--mode", choices=["auto", "manual"], help="Command vehicle operating mode")
    parser.add_argument("--drive", type=int, default=None, help="Stream drive setpoint in mm/s")
    parser.add_argument("--duration", type=float, default=3.0, help="Drive streaming duration in seconds")
    parser.add_argument("--full-cycle", action="store_true", help="Run full cycle: reset -> power on -> auto mode -> drive 1000 mm/s")
    args = parser.parse_args()

    if args.session:
        ensure_session_active(args.url)

    if args.reset or args.full_cycle:
        reset_estop(args.url)

    if args.power or args.full_cycle:
        pwr_on = (args.power == "on") if args.power else True
        set_power(args.url, pwr_on)

    if args.mode or args.full_cycle:
        auto_mode = (args.mode == "auto") if args.mode else True
        set_mode(args.url, auto_mode)

    if args.drive is not None:
        stream_drive(args.url, args.drive, duration_s=args.duration)
    elif args.full_cycle:
        stream_drive(args.url, 1000, duration_s=args.duration)

if __name__ == "__main__":
    main()
