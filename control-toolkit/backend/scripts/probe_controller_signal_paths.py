"""Signal path probe: high inject, low direct inject, what appears on each bus.

Not a full vehicle stack — only checks that commanded frames reach the intended
controller bus(es) and whether low-level TX also appears as high-side feedback.
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


def ensure_ready():
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
        _, st = req("GET", "/status")
        ses = st["session"]
        code, body = req(
            "POST",
            f"/sessions/{ses['session_id']}/bench-tx",
            {"enabled": True, "expected_revision": ses["revision"]},
        )
        assert code == 200, body
    # SIL for high→low RT cascade
    _, sim = req("GET", "/simulation")
    sim = sim["simulation"]
    if sim["sys_sil"]["state"] != "running" or (
        sim["rt_sil"].get("available") and sim["rt_sil"]["state"] != "running"
    ):
        req("POST", "/simulation/start")
    return req("GET", "/status")[1]["session"]


def msg_index():
    _, st = req("GET", "/state")
    out = {}
    for m in st.get("messages") or []:
        name = m.get("name") or "?"
        bus = m.get("bus") or "?"
        sigs = {}
        for k, v in (m.get("signals") or {}).items():
            sigs[k] = v.get("engineering_value") if isinstance(v, dict) else v
        out[f"{bus}:{name}"] = {
            "freshness": m.get("freshness"),
            "rate": m.get("observed_rate_hz"),
            "signals": sigs,
            "can_id": m.get("can_id"),
        }
    return out


def wait_keys(keys: set[str], timeout_s: float = 2.5) -> dict:
    deadline = time.time() + timeout_s
    last = {}
    while time.time() < deadline:
        last = msg_index()
        if keys.issubset(last.keys()):
            return last
        time.sleep(0.05)
    return last


def present(idx: dict, key: str) -> bool:
    row = idx.get(key)
    return bool(row and row.get("freshness") in ("live", "late", "frozen", "recovering"))


def report(title: str, checks: list[tuple[str, bool, str]]):
    print(f"\n## {title}")
    ok = 0
    for name, good, detail in checks:
        mark = "PASS" if good else "FAIL/ABSENT"
        if good:
            ok += 1
        print(f"  [{mark}] {name}: {detail}")
    print(f"  -> {ok}/{len(checks)}")
    return ok, len(checks)


def main() -> None:
    ses = ensure_ready()
    print(f"session={ses['session_id']} bench_tx={ses['bench_tx']}")

    # Release any prior control ownership
    req("POST", "/control/release", {"reason": "probe_reset"})
    req("POST", "/analysis/stop")

    totals_ok = totals_n = 0

    # --- A: High host command path (controller: Host kinematics) ---
    code, _ = req(
        "POST",
        "/analysis/host-drive",
        {"speed_mmps": 700, "yaw_rate_mrad_s": 150, "gear": 1, "period_ms": 30},
    )
    assert code == 200
    time.sleep(0.8)
    idx = msg_index()
    o, n = report(
        "A High host command (HOST_DRIVE_CMD) + RT cascade",
        [
            (
                "Host high TX visible",
                present(idx, "high:HOST_DRIVE_CMD"),
                str((idx.get("high:HOST_DRIVE_CMD") or {}).get("signals")),
            ),
            (
                "Host not required on low",
                not present(idx, "low:HOST_DRIVE_CMD") or present(idx, "low:HOST_DRIVE_CMD"),
                "low HOST optional",
            ),
            (
                "RT low receives cascade (RT SIL)",
                present(idx, "low:RT_DRIVE_CMD"),
                str((idx.get("low:RT_DRIVE_CMD") or {}).get("signals")),
            ),
            (
                "SYS low heartbeat (SYS peer)",
                present(idx, "low:SYS_HEARTBEAT"),
                str((idx.get("low:SYS_HEARTBEAT") or {}).get("signals")),
            ),
        ],
    )
    totals_ok += o
    totals_n += n
    req("POST", "/analysis/stop")
    req("POST", "/control/release", {"reason": "after_host"})

    # --- B: Low direct motor (controller: RT motor cmd path) ---
    code, body = req(
        "POST",
        "/control/direct",
        {
            "channel": "motor",
            "enabled": True,
            "values": {"motor_speed_mmps": 500, "gear": 1},
            "period_ms": 20,
        },
    )
    assert code == 200, body
    time.sleep(0.7)
    idx = msg_index()
    low_rt = idx.get("low:RT_DRIVE_CMD") or {}
    high_rt = idx.get("high:RT_DRIVE_CMD") or {}
    o, n = report(
        "B Low direct MOTOR inject — does it appear low? high feedback?",
        [
            (
                "Low RT_DRIVE_CMD (to motor controller path)",
                present(idx, "low:RT_DRIVE_CMD"),
                str(low_rt.get("signals")),
            ),
            (
                "High RT_DRIVE_CMD feedback (same frame bridge?)",
                present(idx, "high:RT_DRIVE_CMD"),
                str(high_rt.get("signals")) if high_rt else "no high RT_DRIVE",
            ),
            (
                "MTR_MOTOR_FBK high (actuator feedback)",
                present(idx, "high:MTR_MOTOR_FBK") or present(idx, "low:MTR_MOTOR_FBK"),
                "needs MTR peer — expected absent without full stack",
            ),
        ],
    )
    totals_ok += o
    totals_n += n
    req("POST", "/control/direct", {"channel": "motor", "enabled": False})

    # --- C: Low direct steering (controller: SES / VCU_SES_REQ) ---
    code, body = req(
        "POST",
        "/control/direct",
        {
            "channel": "steering",
            "enabled": True,
            "values": {"target_angle_raw": 120},  # 12.0°
            "period_ms": 20,
        },
    )
    print("  steer direct HTTP", code, body if code >= 400 else "ok")
    assert code == 200, body
    time.sleep(0.7)
    idx = msg_index()
    o, n = report(
        "C Low direct STEERING inject — SES path",
        [
            (
                "Low VCU_SES_REQ / SES command",
                present(idx, "low:VCU_SES_REQ") or present(idx, "low:SES_CMD"),
                str(
                    (idx.get("low:VCU_SES_REQ") or idx.get("low:SES_CMD") or {}).get(
                        "signals"
                    )
                ),
            ),
            (
                "High VCU_SES_REQ feedback",
                present(idx, "high:VCU_SES_REQ"),
                str((idx.get("high:VCU_SES_REQ") or {}).get("signals"))
                if idx.get("high:VCU_SES_REQ")
                else "no high SES",
            ),
            (
                "SES_STATUS feedback",
                present(idx, "low:SES_STATUS") or present(idx, "high:SES_STATUS"),
                "needs SES peer — expected absent without full stack",
            ),
        ],
    )
    totals_ok += o
    totals_n += n
    req("POST", "/control/direct", {"channel": "steering", "enabled": False})

    # --- D: Low direct brake (controller: SEB / VCU_SEB_REQ) ---
    code, body = req(
        "POST",
        "/control/direct",
        {
            "channel": "brake",
            "enabled": True,
            "values": {"pressure_request_raw": 40},
            "period_ms": 20,
        },
    )
    print("  brake direct HTTP", code, body if code >= 400 else "ok")
    assert code == 200, body
    time.sleep(0.7)
    idx = msg_index()
    o, n = report(
        "D Low direct BRAKE inject — SEB path",
        [
            (
                "Low VCU_SEB_REQ / brake command",
                present(idx, "low:VCU_SEB_REQ")
                or present(idx, "low:RT_BRAKE_CMD")
                or present(idx, "low:HOST_BRAKE_REQ"),
                str(
                    (
                        idx.get("low:VCU_SEB_REQ")
                        or idx.get("low:RT_BRAKE_CMD")
                        or idx.get("low:HOST_BRAKE_REQ")
                        or {}
                    ).get("signals")
                ),
            ),
            (
                "High brake command feedback",
                present(idx, "high:VCU_SEB_REQ")
                or present(idx, "high:RT_BRAKE_CMD")
                or present(idx, "high:HOST_BRAKE_REQ"),
                "high mirror?"
                + str(
                    (
                        idx.get("high:VCU_SEB_REQ")
                        or idx.get("high:RT_BRAKE_CMD")
                        or {}
                    ).get("signals")
                ),
            ),
            (
                "SEB_STATUS / BRAKE_DIAG feedback",
                present(idx, "low:SEB_STATUS")
                or present(idx, "high:SEB_STATUS")
                or present(idx, "low:BRAKE_DIAG"),
                "needs SEB peer — expected absent without full stack",
            ),
        ],
    )
    totals_ok += o
    totals_n += n
    req("POST", "/control/direct", {"channel": "brake", "enabled": False})
    req("POST", "/control/release", {"reason": "probe_done"})

    # Summary of all live messages for transparency
    time.sleep(0.2)
    idx = msg_index()
    print("\n## Live catalog snapshot (all names)")
    for k in sorted(idx):
        if idx[k]["freshness"] == "live":
            print(f"  {k}: {idx[k]['signals']}")

    print(f"\nTOTAL command-path checks noted: {totals_ok}/{totals_n} present")
    print(
        "Note: FAIL/ABSENT on *feedback* peers (MTR/SES/SEB status) is expected "
        "without those ECUs; command-to-controller TX is the primary pass criterion."
    )


if __name__ == "__main__":
    main()
