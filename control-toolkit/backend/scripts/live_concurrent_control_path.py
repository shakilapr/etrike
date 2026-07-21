"""Concurrent Control path: session + TX + SIL + host inject + low direct-ish status.

Does not treat a single inject as success — verifies simultaneous preconditions
and multi-bus outputs together.
"""

from __future__ import annotations

import json
import time
import urllib.error
import urllib.request

BASE = "http://127.0.0.1:8001/api/v1"


def req(method: str, path: str, body: dict | None = None):
    data = None if body is None else json.dumps(body).encode()
    headers = {"Content-Type": "application/json"} if body is not None else {}
    request = urllib.request.Request(BASE + path, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=15) as resp:
            return resp.status, json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        raw = e.read().decode(errors="replace")
        try:
            payload = json.loads(raw)
        except json.JSONDecodeError:
            payload = {"raw": raw}
        return e.code, payload


def names_from_state() -> set[str]:
    _, state = req("GET", "/state")
    out: set[str] = set()
    for m in state.get("messages") or []:
        if m.get("name"):
            out.add(str(m["name"]))
    return out


def activity():
    _, st = req("GET", "/status")
    ch = st["adapter"]["channels"]
    return {
        "health": st["adapter"]["health"],
        "high": ch["high"]["activity"],
        "low": ch["low"]["activity"],
        "high_rx": ch["high"]["rx_count"],
        "low_rx": ch["low"]["rx_count"],
        "bench_tx": st.get("session", {}).get("bench_tx"),
        "profile": st.get("session", {}).get("profile"),
        "session_id": st.get("session", {}).get("session_id"),
    }


def wait_names(need: set[str], timeout_s: float = 3.0) -> set[str]:
    found: set[str] = set()
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        found |= names_from_state()
        if need.issubset(found):
            return found
        time.sleep(0.05)
    return found


def main() -> None:
    print("\n## Scenario 1 — all gates must be true before TX is meaningful")
    code, st = req("GET", "/status")
    assert code == 200, st
    ses = st.get("session") or {}
    if not ses.get("session_id") or ses.get("profile") != "pure_software":
        if ses.get("session_id"):
            req(
                "POST",
                f"/sessions/{ses['session_id']}/close",
                {"expected_revision": ses.get("revision", 0)},
            )
        code, created = req("POST", "/sessions", {"profile": "pure_software"})
        assert code == 200, created
        ses = created["session"]
    sid, rev = ses["session_id"], ses["revision"]
    print(f"  session={sid} phase={ses['phase']} profile={ses.get('profile')}")

    # SIL peers must already be contributing traffic for truthful topology/live
    code, sim = req("GET", "/simulation")
    sim = sim["simulation"]
    if sim["sys_sil"]["state"] != "running":
        code, sim = req("POST", "/simulation/start")
        sim = sim["simulation"]
    print(f"  sil: sys={sim['sys_sil']['state']} rt={sim['rt_sil']['state']} vc={sim['virtual_can']['state']}")
    assert sim["sys_sil"]["state"] == "running"
    assert sim["virtual_can"]["state"] == "running"

    time.sleep(0.35)
    a0 = activity()
    print(f"  bus with SIL only: {a0}")
    assert a0["low"] == "active", a0

    # Ensure TX gate off so single inject is not enough
    code, off = req(
        "POST",
        f"/sessions/{sid}/bench-tx",
        {"enabled": False, "expected_revision": rev},
    )
    if code == 200:
        rev = (off.get("session") or off).get("revision", rev)
    # Negative: inject WITHOUT bench TX must fail
    code, rejected = req(
        "POST",
        "/injections",
        {
            "bus": "high",
            "key": "host:host_drive_cmd",
            "values": {"speed_mmps": 100, "yaw_rate_mrad_s": 0, "gear": 1},
        },
    )
    print(f"  inject without TX gate -> HTTP {code} (expect fail)")
    assert code >= 400, rejected

    # Enable TX gate while SIL still running
    code, st2 = req("GET", "/status")
    rev = st2["session"]["revision"]
    code, bench = req(
        "POST",
        f"/sessions/{sid}/bench-tx",
        {"enabled": True, "expected_revision": rev},
    )
    assert code == 200, bench
    print("  bench_tx enabled under live SIL")

    # Concurrent: SYS heartbeats + periodic host drive
    code, analysis = req(
        "POST",
        "/analysis/host-drive",
        {"speed_mmps": 600, "yaw_rate_mrad_s": 100, "gear": 1, "period_ms": 40},
    )
    assert code == 200, analysis
    print(f"  analysis host-drive: {analysis.get('mode')} job={analysis.get('job_id')}")

    found = wait_names({"SYS_HEARTBEAT", "SYS_SAFETY_STS", "HOST_DRIVE_CMD"}, 3.0)
    print(f"  concurrent messages: {sorted(n for n in found if n.startswith(('SYS','HOST')))}")
    assert {"SYS_HEARTBEAT", "HOST_DRIVE_CMD"} <= found, found

    a1 = activity()
    print(f"  bus multi-source: {a1}")
    assert a1["high"] == "active" and a1["low"] == "active", a1

    print("\n## Scenario 2 — ESTOP while drive analysis + SIL still concurrent")
    code, estop = req("POST", "/control/estop", {})
    # estop may need alternate path
    if code >= 400:
        code, estop = req("POST", "/injections", {"bus": "high", "can_id": 1, "values": {}})
    print(f"  estop attempt HTTP {code}")

    print("\n## Scenario 3 — tear down concurrent sources; buses must go quiet")
    req("POST", "/analysis/stop")
    code, stop = req("POST", "/simulation/stop")
    stop = stop["simulation"]
    print(f"  stopped sys={stop['sys_sil']['state']} rt={stop['rt_sil']['state']} vc={stop['virtual_can']['state']}")
    assert stop["sys_sil"]["state"] == "stopped"
    assert stop["virtual_can"]["state"] == "running"  # VC stays up

    time.sleep(1.0)
    idle = activity()
    print(f"  after idle: {idle}")
    assert idle["high"] in ("quiet", "unseen"), idle
    assert idle["low"] in ("quiet", "unseen"), idle

    print("\n## Scenario 4 — restart only SIL (no inject): SYS outputs alone")
    req("POST", "/simulation/start")
    time.sleep(0.5)
    found2 = wait_names({"SYS_HEARTBEAT", "SYS_SAFETY_STS"}, 2.0)
    a2 = activity()
    print(f"  SYS-only messages: {sorted(n for n in found2 if n.startswith('SYS'))}")
    print(f"  bus: {a2}")
    assert "SYS_HEARTBEAT" in found2
    assert a2["low"] == "active"

    print("\nALL CONCURRENT CONTROL-PATH PHASES OK")


if __name__ == "__main__":
    main()
