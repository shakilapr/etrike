"""Probe whether High HOST commands cascade to Low RT/MTR/SYS outputs."""

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
        return e.code, json.loads(e.read().decode(errors="replace"))


def index_messages():
    _, st = req("GET", "/state")
    out = {}
    for m in st.get("messages") or []:
        name = m.get("name") or "?"
        bus = m.get("bus")
        key = f"{bus}:{name}"
        sigs = {}
        for k, v in (m.get("signals") or {}).items():
            sigs[k] = v.get("engineering_value") if isinstance(v, dict) else v
        out[key] = {
            "freshness": m.get("freshness"),
            "rate": m.get("observed_rate_hz"),
            "signals": sigs,
            "can_id": m.get("can_id"),
        }
    return out


def ensure_session_and_tx():
    _, st = req("GET", "/status")
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
    if ses.get("bench_tx") != "enabled":
        code, body = req(
            "POST",
            f"/sessions/{ses['session_id']}/bench-tx",
            {"enabled": True, "expected_revision": ses.get("revision", 0)},
        )
        assert code == 200, body
        ses = body.get("session") or req("GET", "/status")[1]["session"]
    return ses


def main() -> None:
    ses = ensure_session_and_tx()
    print("session", ses["session_id"], "bench_tx", ses["bench_tx"])

    _, sim = req("GET", "/simulation")
    sim = sim["simulation"]
    print("SIL before", "sys", sim["sys_sil"]["state"], "rt", sim["rt_sil"]["state"])
    if sim["sys_sil"]["state"] != "running" or (
        sim["rt_sil"]["available"] and sim["rt_sil"]["state"] != "running"
    ):
        req("POST", "/simulation/start")
        _, sim = req("GET", "/simulation")
        sim = sim["simulation"]
    print("SIL", "sys", sim["sys_sil"]["state"], "rt", sim["rt_sil"]["state"])

    print("\n=== INPUT: periodic HOST_DRIVE_CMD on HIGH (speed+yaw) ===")
    code, analysis = req(
        "POST",
        "/analysis/host-drive",
        {"speed_mmps": 900, "yaw_rate_mrad_s": 250, "gear": 1, "period_ms": 20},
    )
    print("analysis", code, analysis.get("mode"), analysis.get("job_id"))

    time.sleep(1.5)
    after = index_messages()

    chain = [
        "high:HOST_DRIVE_CMD",
        "low:RT_DRIVE_CMD",
        "high:RT_DRIVE_CMD",
        "low:MTR_MOTOR_FBK",
        "high:MTR_MOTOR_FBK",
        "low:VCU_SES_REQ",
        "high:VCU_SES_REQ",
        "low:VCU_SEB_REQ",
        "high:VCU_SEB_REQ",
        "low:SES_STATUS",
        "low:SEB_STATUS",
        "low:SYS_HEARTBEAT",
        "low:SYS_SAFETY_STS",
        "high:SYS_SAFETY_STS",
        "low:HOST_BRAKE_REQ",
        "high:HOST_BRAKE_REQ",
        "low:RT_BRAKE_CMD",
        "high:RT_BRAKE_CMD",
    ]
    print("\n=== CHAIN VISIBILITY (High inject → Low controllers?) ===")
    for key in chain:
        row = after.get(key)
        if not row:
            print(f"  MISSING  {key}")
        else:
            print(
                f"  PRESENT  {key}  fresh={row['freshness']} rate={row['rate']}  {row['signals']}"
            )

    host = after.get("high:HOST_DRIVE_CMD", {})
    rt = after.get("low:RT_DRIVE_CMD", {})
    print("\n=== CORRECTNESS SPOT CHECK ===")
    print("  HOST high signals:", host.get("signals"))
    print("  RT_DRIVE low signals:", rt.get("signals"))
    if host.get("signals") and rt.get("signals"):
        hs = host["signals"].get("speed_mmps")
        rs = rt["signals"].get("speed_mmps") or rt["signals"].get("cmd_speed_mmps")
        print(f"  speed host={hs} vs rt_low={rs}  (cascade if both present and related)")
    else:
        print("  Cannot compare speeds — RT low missing or incomplete (RT SIL may not fully cascade)")

    print("\n=== CONTINUITY (sample rates over 1s) ===")
    t0 = time.time()
    samples = []
    while time.time() - t0 < 1.0:
        m = index_messages()
        samples.append(
            {
                "host": (m.get("high:HOST_DRIVE_CMD") or {}).get("rate"),
                "rt": (m.get("low:RT_DRIVE_CMD") or {}).get("rate"),
                "sys": (m.get("low:SYS_HEARTBEAT") or {}).get("rate"),
            }
        )
        time.sleep(0.2)
    print("  rate samples:", samples)

    print("\n=== CLEANUP stop analysis ===")
    req("POST", "/analysis/stop")
    print("done")


if __name__ == "__main__":
    main()
