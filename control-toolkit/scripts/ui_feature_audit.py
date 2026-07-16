#!/usr/bin/env python3
"""Exercise every control-toolkit API feature the UI depends on.

Reports PASS/FAIL per feature. Use against a running backend:
  python control-toolkit/scripts/ui_feature_audit.py --base http://127.0.0.1:8001
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

_BACKEND = Path(__file__).resolve().parents[1] / "backend"
if str(_BACKEND) not in sys.path:
    sys.path.insert(0, str(_BACKEND))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", default="http://127.0.0.1:8001")
    args = parser.parse_args()
    base = args.base.rstrip("/")

    try:
        import httpx
    except ImportError:
        print("httpx required: pip install httpx")
        return 2

    client = httpx.Client(base_url=base, timeout=30.0)
    results: list[tuple[str, bool, str]] = []

    def check(name: str, fn) -> None:
        try:
            detail = fn() or "ok"
            results.append((name, True, str(detail)[:120]))
            print(f"PASS  {name}: {detail}")
        except Exception as exc:  # noqa: BLE001
            results.append((name, False, str(exc)[:200]))
            print(f"FAIL  {name}: {exc}")

    def get(path: str):
        r = client.get(f"/api/v1{path}")
        r.raise_for_status()
        return r.json()

    def post(path: str, json=None):
        r = client.post(f"/api/v1{path}", json=json or {})
        if r.status_code >= 400:
            raise RuntimeError(f"{r.status_code} {r.text[:300]}")
        return r.json()

    def delete(path: str, json=None):
        r = client.request("DELETE", f"/api/v1{path}", json=json)
        if r.status_code >= 400:
            raise RuntimeError(f"{r.status_code} {r.text[:300]}")
        return r.json()

    check("status", lambda: f"ready={get('/status').get('ready')}")
    check("dictionary", lambda: f"msgs={get('/protocol/dictionary').get('count')}")
    check("protocol messages", lambda: f"n={get('/protocol/messages').get('count')}")
    check("topology", lambda: f"nodes={len(get('/topology').get('nodes') or [])}")
    check("state", lambda: f"seq={get('/state').get('sequence')}")
    check("history", lambda: f"frames={len(get('/history?limit=10').get('frames') or [])}")
    check("events", lambda: f"n={get('/events?limit=5').get('count')}")
    check("episodes", lambda: f"n={get('/episodes').get('count')}")
    check("profiles", lambda: ",".join(p["id"] for p in get("/sessions/profiles")["profiles"]))

    def session_flow():
        cur = get("/sessions")["session"]
        if cur.get("session_id"):
            delete(
                f"/sessions/{cur['session_id']}",
                {"expected_revision": cur["revision"], "outcome": "stopped"},
            )
        ses = post("/sessions", {"profile": "pure_software"})["session"]
        sid, rev = ses["session_id"], ses["revision"]
        ses = post(
            f"/sessions/{sid}/bench-tx",
            {"enabled": True, "expected_revision": rev},
        )["session"]
        return ses

    check("session+bench_tx", lambda: f"id={session_flow()['session_id'][:12]} tx={session_flow() and get('/sessions')['session']['bench_tx']}")

    # Ensure one session ready
    try:
        ses = get("/sessions")["session"]
        if not ses.get("session_id") or ses.get("bench_tx") != "enabled":
            ses = session_flow()
    except Exception as exc:  # noqa: BLE001
        results.append(("session ready", False, str(exc)[:200]))
        print(f"FAIL  session ready: {exc}")
        failed = [r for r in results if not r[1]]
        print(f"Summary: {len(results) - len(failed)}/{len(results)} passed (aborted)")
        return 1

    check(
        "inject HOST_DRIVE",
        lambda: post(
            "/injections",
            {
                "bus": "high",
                "key": "host:host_drive_cmd",
                "values": {"speed_mmps": 400, "yaw_rate_mrad_s": 50, "gear": 1},
                "owner": "audit:inject",
            },
        ).get("disposition")
        or "ok",
    )
    time.sleep(0.2)
    check(
        "state sees HOST_DRIVE_CMD",
        lambda: next(
            m["name"]
            for m in get("/state")["messages"]
            if m.get("name") == "HOST_DRIVE_CMD"
        ),
    )
    check(
        "control intent",
        lambda: post(
            "/control/intent",
            {
                "sequence": int(time.time()) % 100000,
                "throttle": 0.2,
                "steer": 0.1,
                "mode": "kinematics",
            },
        )["control"].get("method"),
    )
    check(
        "control direct motor",
        lambda: post(
            "/control/direct",
            {
                "channel": "motor",
                "enabled": True,
                "values": {"motor_speed_mmps": 120, "gear": 1},
            },
        )["control"].get("method"),
    )
    check(
        "hmi mode",
        lambda: str(post("/hmi/mode", {"req_mode": 0, "enabled": True}).get("ok", True)),
    )
    check(
        "hmi power",
        lambda: str(post("/hmi/power", {"req_start": 1, "enabled": True}).get("ok", True)),
    )
    # Same owner as prior inject so ownership does not 409.
    check(
        "analysis host-drive",
        lambda: post(
            "/analysis/host-drive",
            {"speed_mmps": 10, "yaw_rate_mrad_s": 0, "gear": 0},
        ).get("mode")
        if True
        else "",
    )
    # Prefer control release first so analysis can claim 0x300.
    try:
        post("/control/release", {"reason": "audit_pre_analysis"})
        # free ownership by stop analysis / re-inject under analysis owner
        post(
            "/injections",
            {
                "bus": "high",
                "key": "host:host_drive_cmd",
                "values": {"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
                "owner": "analysis:host_drive",
            },
        )
    except Exception:
        pass
    rec = post("/recordings", {})["recording"]
    check("recording start", lambda: rec["recording_id"])
    time.sleep(0.15)
    check(
        "recording stop",
        lambda: delete(f"/recordings/{rec['recording_id']}")["recording"][
            "evidence_quality"
        ],
    )
    rid = rec["recording_id"]
    check(
        "evidence window",
        lambda: f"n={get(f'/evidence/{rid}?limit=5').get('frame_total')}",
    )
    check(
        "sequential test",
        lambda: post(
            "/tests",
            {
                "name": "audit",
                "owner": "audit:inject",
                "stimulus": {
                    "type": "inject",
                    "bus": "high",
                    "key": "host:host_drive_cmd",
                    "values": {"speed_mmps": 5, "yaw_rate_mrad_s": 0, "gear": 0},
                },
                "expect": {
                    "type": "message_observed",
                    "bus": "high",
                    "can_id": 0x300,
                    "timeout_ms": 500,
                },
            },
        )["test"]["disposition"],
    )
    check(
        "dict refresh",
        lambda: f"n={post('/protocol/dictionary/refresh').get('count')}",
    )
    check(
        "layout 0x300",
        lambda: get("/protocol/messages/high/0x300/layout")["bit_grid"]["dlc"],
    )
    check(
        "control release",
        lambda: post("/control/release", {"reason": "audit"})["control"].get("mode"),
    )
    ses = get("/sessions")["session"]
    check(
        "stop all",
        lambda: post(
            f"/sessions/{ses['session_id']}/stop-all",
            {"expected_revision": ses["revision"]},
        )["session"]["bench_tx"],
    )

    # Physical probe
    profs = {p["id"]: p for p in get("/sessions/profiles")["profiles"]}
    phys = profs.get("full_vehicle", {})
    check(
        "physical profile probe",
        lambda: (
            f"available={phys.get('available')} reason={phys.get('reason')}"
        ),
    )

    failed = [r for r in results if not r[1]]
    print()
    print(f"Summary: {len(results) - len(failed)}/{len(results)} passed")
    if failed:
        print("Failures:")
        for n, _, d in failed:
            print(f"  - {n}: {d}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
