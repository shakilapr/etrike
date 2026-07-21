"""Live multi-phase concurrent I/O check against a running toolkit API."""

from __future__ import annotations

import json
import time
import urllib.request

BASE = "http://127.0.0.1:8001/api/v1"


def req(method: str, path: str, body: dict | None = None):
    data = None if body is None else json.dumps(body).encode()
    headers = {"Content-Type": "application/json"} if body is not None else {}
    request = urllib.request.Request(BASE + path, data=data, method=method, headers=headers)
    with urllib.request.urlopen(request, timeout=15) as resp:
        return json.loads(resp.read().decode())


def activity():
    st = req("GET", "/status")
    ch = st["adapter"]["channels"]
    return {
        "health": st["adapter"]["health"],
        "high": ch["high"]["activity"],
        "low": ch["low"]["activity"],
        "high_rx": ch["high"]["rx_count"],
        "low_rx": ch["low"]["rx_count"],
    }


def main() -> None:
    print("=== PHASE A: ensure Computer session ===")
    st = req("GET", "/status")
    ses = st.get("session") or {}
    if not ses.get("session_id") or ses.get("profile") != "pure_software":
        # Close existing if needed, then create pure_software
        if ses.get("session_id"):
            try:
                req(
                    "POST",
                    f"/sessions/{ses['session_id']}/close",
                    {"expected_revision": ses.get("revision", 0)},
                )
            except Exception as exc:
                print("  close prior session:", exc)
        ses = req("POST", "/sessions", {"profile": "pure_software"})["session"]
    else:
        print("  reusing existing session")
    print("session", ses["session_id"], "phase", ses["phase"], "bench_tx", ses["bench_tx"])

    print("=== PHASE B: ensure SIL running (SYS+RT concurrent peers) ===")
    sim = req("GET", "/simulation")["simulation"]
    print("sys", sim["sys_sil"]["state"], "rt", sim["rt_sil"]["state"])
    if sim["sys_sil"]["state"] != "running":
        sim = req("POST", "/simulation/start")["simulation"]
        print("started sys", sim["sys_sil"]["state"], "rt", sim["rt_sil"]["state"])

    time.sleep(0.5)
    print("activity with SIL:", activity())

    print("=== PHASE C: enable Bench TX (required with SIL for inject) ===")
    st = req("GET", "/status")["session"]
    bench = req(
        "POST",
        f"/sessions/{st['session_id']}/bench-tx",
        {"enabled": True, "expected_revision": st["revision"]},
    )
    print("bench", bench.get("session", bench))

    print("=== PHASE D: concurrent HOST drive analysis while SYS emits heartbeat ===")
    inj = req(
        "POST",
        "/analysis/host-drive",
        {"speed_mmps": 550, "yaw_rate_mrad_s": 80, "gear": 1, "period_ms": 50},
    )
    print("analysis", {k: inj.get(k) for k in ("ok", "mode", "job_id", "status")})

    deadline = time.time() + 2
    names: set[str] = set()
    while time.time() < deadline:
        state = req("GET", "/state")
        for m in state.get("messages") or []:
            if m.get("name"):
                names.add(str(m["name"]))
        if {"SYS_HEARTBEAT", "SYS_SAFETY_STS", "HOST_DRIVE_CMD"} <= names:
            break
        time.sleep(0.05)
    print(
        "observed messages:",
        sorted(n for n in names if n.startswith("SYS") or n.startswith("HOST")),
    )
    print("activity multi-source:", activity())

    print("=== PHASE E: stop analysis + SIL together ===")
    print("stop analysis", req("POST", "/analysis/stop"))
    stop = req("POST", "/simulation/stop")["simulation"]
    print(
        "stop sim",
        {k: stop[k]["state"] for k in ("sys_sil", "rt_sil", "virtual_can")},
    )

    print("waiting 1.0s for quiet aging...")
    time.sleep(1.0)
    idle = activity()
    print("activity idle (expect quiet):", idle)
    assert idle["high"] in ("quiet", "unseen"), idle
    assert idle["low"] in ("quiet", "unseen"), idle
    assert idle["health"] in ("quiet", "open"), idle

    print("=== PHASE F: restart SIL — buses active again ===")
    req("POST", "/simulation/start")
    time.sleep(0.5)
    again = activity()
    print("activity after restart:", again)
    assert again["low"] == "active", again
    print("ALL LIVE PHASES OK")


if __name__ == "__main__":
    main()
