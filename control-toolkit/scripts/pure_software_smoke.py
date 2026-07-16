#!/usr/bin/env python3
"""Headless Pure Software smoke (software-track exit gate).

Drives the Control Toolkit HTTP API without React:
  session → Bench TX → inject HOST_DRIVE_CMD → verify state →
  recording + sequential test → Stop All.

Usage (backend already running on 8001):
  python control-toolkit/scripts/pure_software_smoke.py --base http://127.0.0.1:8001

Or spawn an in-process app (default):
  python control-toolkit/scripts/pure_software_smoke.py
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

# Allow running from repo root without install.
_BACKEND = Path(__file__).resolve().parents[1] / "backend"
if str(_BACKEND) not in sys.path:
    sys.path.insert(0, str(_BACKEND))


def _httpx_client(base: str):
    import httpx

    return httpx.Client(base_url=base.rstrip("/"), timeout=30.0)


def _inprocess_client():
    from fastapi.testclient import TestClient

    from control_toolkit.config import ToolkitConfig
    from control_toolkit.main import create_app

    app = create_app(ToolkitConfig())
    return TestClient(app)


def run(client) -> int:
    def get(path: str):
        r = client.get(f"/api/v1{path}")
        r.raise_for_status()
        return r.json()

    def post(path: str, json=None):
        r = client.post(f"/api/v1{path}", json=json or {})
        r.raise_for_status()
        return r.json()

    def delete(path: str, json=None):
        r = client.request("DELETE", f"/api/v1{path}", json=json)
        r.raise_for_status()
        return r.json()

    status = get("/status")
    assert status.get("ready") is True, status
    print(f"OK  ready wire={str(status.get('wire_hash', ''))[:12]}")

    ses = post("/sessions", {"profile": "pure_software"})["session"]
    sid = ses["session_id"]
    rev = ses["revision"]
    print(f"OK  session {sid} rev={rev}")

    ses = post(
        f"/sessions/{sid}/bench-tx",
        {"enabled": True, "expected_revision": rev},
    )["session"]
    rev = ses["revision"]
    assert ses["bench_tx"] in ("enabled", "Enabled") or ses.get("bench_tx") == "enabled"
    print("OK  Bench TX enabled")

    rec = post("/recordings")["recording"]
    rid = rec["recording_id"]
    print(f"OK  recording {rid}")

    owner = "script:smoke"
    inj = post(
        "/injections",
        {
            "bus": "high",
            "key": "host:host_drive_cmd",
            "values": {"speed_mmps": 333, "yaw_rate_mrad_s": 12, "gear": 1},
            "owner": owner,
        },
    )
    print(f"OK  inject disposition={inj.get('disposition')}")

    time.sleep(0.15)
    state = get("/state")
    host = next(
        (m for m in state["messages"] if m.get("name") == "HOST_DRIVE_CMD"),
        None,
    )
    assert host is not None, "HOST_DRIVE_CMD not in latest state"
    print(f"OK  state sees HOST_DRIVE_CMD freshness={host.get('freshness')}")

    # Same owner as prior inject so ownership does not conflict.
    test = post(
        "/tests",
        {
            "name": "script_speed_equals",
            "owner": owner,
            "stimulus": {
                "type": "inject",
                "bus": "high",
                "key": "host:host_drive_cmd",
                "values": {"speed_mmps": 500, "yaw_rate_mrad_s": 0, "gear": 1},
            },
            "expect": {
                "type": "signal_equals",
                "bus": "high",
                "can_id": 0x300,
                "signal": "speed_mmps",
                "equals": 500,
                "timeout_ms": 800,
            },
        },
    )["test"]
    assert test["disposition"] == "pass", test
    print(f"OK  sequential test pass id={test['test_id']}")

    stop_rec = delete(f"/recordings/{rid}")
    print(
        f"OK  recording stopped frames={stop_rec['recording']['frame_count']} "
        f"quality={stop_rec['recording']['evidence_quality']}"
    )

    ev = get(f"/evidence/{rid}?limit=10")
    assert ev["frame_total"] >= 1
    print(f"OK  evidence window frames={ev['frame_total']}")

    layout = get("/protocol/messages/high/0x300/layout")
    assert layout["bit_grid"]["dlc"] == 8
    print(f"OK  bit grid dlc={layout['bit_grid']['dlc']} fields={len(layout['bit_grid']['fields'])}")

    # refresh session rev
    ses = get("/sessions")["session"]
    rev = ses["revision"]
    post(f"/sessions/{sid}/stop-all", {"expected_revision": rev})
    print("OK  Stop All")

    print("PASS pure_software_smoke")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--base",
        default=None,
        help="HTTP base URL (e.g. http://127.0.0.1:8001). Default: in-process TestClient.",
    )
    args = parser.parse_args()
    if args.base:
        with _httpx_client(args.base) as client:
            return run(client)
    with _inprocess_client() as client:
        return run(client)


if __name__ == "__main__":
    raise SystemExit(main())