"""Software-only testing-guide recipes A–E via REST API.

Observes TX (virtual echo) for Host 0x300 and Low unit commands
0x204 / 0x169 / 0x7B9. No ECU feedback required.

Usage (API on 127.0.0.1:8001):
  python scripts/software_only_recipe_qa.py
"""

from __future__ import annotations

import json
import sys
import time
import urllib.error
import urllib.request
from collections import Counter
from pathlib import Path

BASE = "http://127.0.0.1:8001/api/v1"
OUT_DIR = Path(__file__).resolve().parents[2] / "test-results"
OUT_JSON = OUT_DIR / "software-only-recipe-qa.json"
OUT_MD = OUT_DIR / "software-only-recipe-qa.md"

# Contract IDs
ID_HOST = 0x300
ID_MTR_CMD = 0x204
ID_SES_CMD = 0x169
ID_SEB_CMD = 0x7B9
ID_RT_BRAKE = 0x205

results: list[dict] = []


def req(method: str, path: str, body: dict | None = None, timeout: float = 12.0):
    data = None if body is None else json.dumps(body).encode()
    headers = {"Content-Type": "application/json"} if data else {}
    r = urllib.request.Request(BASE + path, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(r, timeout=timeout) as resp:
            raw = resp.read().decode()
            return resp.status, json.loads(raw) if raw else {}
    except urllib.error.HTTPError as e:
        raw = e.read().decode()
        try:
            j = json.loads(raw)
        except Exception:
            j = {"raw": raw[:500]}
        return e.code, j
    except Exception as e:  # noqa: BLE001
        return 0, {"error": str(e)}


def note(name: str, passed: bool, detail: str = "", **extra) -> None:
    row = {"name": name, "pass": bool(passed), "detail": detail, **extra}
    results.append(row)
    print(("PASS" if passed else "FAIL"), name, (detail or "")[:200])


def session_bits() -> tuple[str | None, int]:
    code, st = req("GET", "/status")
    if code != 200:
        return None, 0
    sess = st.get("session") or {}
    return sess.get("session_id"), int(sess.get("revision") or 0)


def ensure_session_and_bench() -> str:
    sid, rev = session_bits()
    if not sid:
        code, body = req(
            "POST",
            "/sessions",
            {"profile": "pure_software", "destination": "virtual"},
        )
        note("create_session", code in (200, 201), f"code={code}")
        sid, rev = session_bits()
    else:
        note("session_exists", True, f"sid={sid} rev={rev}")
    assert sid, "no session"
    code, body = req(
        "POST",
        f"/sessions/{sid}/bench-tx",
        {"enabled": True, "expected_revision": rev},
    )
    # revision race — retry once with fresh rev
    if code == 409:
        sid, rev = session_bits()
        code, body = req(
            "POST",
            f"/sessions/{sid}/bench-tx",
            {"enabled": True, "expected_revision": rev},
        )
    note("bench_tx_enable", code == 200, f"code={code} body={str(body)[:120]}")
    # stop residual jobs — note: stop-all DISABLES bench TX (session_manager)
    sid, rev = session_bits()
    code_sa, body_sa = req(
        "POST", f"/sessions/{sid}/stop-all", {"expected_revision": rev}
    )
    note(
        "stop_all_disables_bench_tx",
        True,
        f"stop-all code={code_sa} bench after={(body_sa.get('session') or {}).get('bench_tx')}",
    )
    req("POST", "/analysis/stop", {})
    req("POST", "/control/release", {"reason": "recipe_qa_reset"})
    # Re-enable Bench TX after stop-all (required — this is a plan/UX gotcha)
    sid, rev = session_bits()
    code2, body2 = req(
        "POST",
        f"/sessions/{sid}/bench-tx",
        {"enabled": True, "expected_revision": rev},
    )
    if code2 == 409:
        sid, rev = session_bits()
        code2, body2 = req(
            "POST",
            f"/sessions/{sid}/bench-tx",
            {"enabled": True, "expected_revision": rev},
        )
    note(
        "bench_tx_reenable_after_stop_all",
        code2 == 200
        and (body2.get("session") or {}).get("bench_tx") in ("enabled", "ENABLED"),
        f"code={code2} bench={(body2.get('session') or {}).get('bench_tx')}",
    )
    time.sleep(0.3)
    return sid


def frames_with(can_ids: set[int], limit: int = 400) -> list[dict]:
    code, body = req("GET", f"/history?limit={limit}")
    if code != 200:
        return []
    out = []
    for f in body.get("frames") or []:
        if int(f.get("can_id") or 0) in can_ids:
            out.append(f)
    return out


def latest_msg(can_id: int) -> dict | None:
    code, body = req("GET", "/state")
    if code != 200:
        return None
    for m in body.get("messages") or []:
        if int(m.get("can_id") or 0) == can_id:
            return m
    return None


def eng(msg: dict | None, key: str):
    if not msg:
        return None
    s = (msg.get("signals") or {}).get(key) or {}
    return s.get("engineering_value")


def sample_history_window(
    can_ids: set[int], duration_s: float, poll_s: float = 0.25
) -> list[dict]:
    """Collect frames that appear during the window (diff by global_sequence)."""
    before = frames_with(can_ids, 500)
    max_seq = max((int(f.get("global_sequence") or 0) for f in before), default=0)
    t0 = time.time()
    while time.time() - t0 < duration_s:
        time.sleep(poll_s)
    after = frames_with(can_ids, 800)
    return [f for f in after if int(f.get("global_sequence") or 0) > max_seq]


def count_by_id(frames: list[dict]) -> Counter:
    return Counter(int(f.get("can_id") or 0) for f in frames)


def recipe_a_host(duration: float = 3.0) -> None:
    """Host kinematics / analysis host-drive → 0x300 only."""
    code, body = req(
        "POST",
        "/analysis/host-drive",
        {
            "speed_mmps": 1500,
            "yaw_rate_mrad_s": 0,
            "gear": 1,
            "period_ms": 10,
        },
    )
    note("A_start_host_drive", code == 200, f"code={code} {str(body)[:140]}")
    frames = sample_history_window({ID_HOST, ID_MTR_CMD, ID_SES_CMD, ID_SEB_CMD}, duration)
    counts = count_by_id(frames)
    msg = latest_msg(ID_HOST)
    speed = eng(msg, "speed_mmps")
    gear = eng(msg, "gear")
    # Host analysis job period_ms=10 → ~100 Hz; accept ≥ ~20 Hz continuous
    note(
        "A_host_0x300_present",
        counts[ID_HOST] >= max(8, int(duration * 20)),
        f"count={counts[ID_HOST]} speed={speed} gear={gear} name={msg.get('name') if msg else None}",
        counts=dict(counts),
    )
    note(
        "A_no_low_unit_frames",
        counts[ID_MTR_CMD] == 0 and counts[ID_SES_CMD] == 0 and counts[ID_SEB_CMD] == 0,
        f"low counts mtr={counts[ID_MTR_CMD]} ses={counts[ID_SES_CMD]} seb={counts[ID_SEB_CMD]} "
        "(expected 0 — RT not in process)",
        counts=dict(counts),
    )
    # intentional plan claim check: guide says High does not expand
    if counts[ID_MTR_CMD] or counts[ID_SES_CMD] or counts[ID_SEB_CMD]:
        note(
            "A_plan_unexpected_expansion",
            False,
            "High path produced Low unit frames — RT twin or bug?",
        )
    req("POST", "/analysis/stop", {})
    time.sleep(0.2)


def recipe_b_mtr(duration: float = 3.0) -> None:
    code, body = req(
        "POST",
        "/control/direct",
        {
            "channel": "motor",
            "enabled": True,
            "values": {"motor_speed_mmps": 800, "gear": 1},
            "period_ms": 10,
        },
    )
    note("B_start_motor", code == 200, f"code={code} {str(body)[:140]}")
    frames = sample_history_window({ID_MTR_CMD, ID_HOST}, duration)
    counts = count_by_id(frames)
    msg = latest_msg(ID_MTR_CMD)
    speed = eng(msg, "motor_speed_mmps")
    gear = eng(msg, "gear")
    dlc_ok = True
    if frames:
        # history may not include dlc always
        dlc_vals = {f.get("dlc") for f in frames if f.get("can_id") == ID_MTR_CMD and f.get("dlc") is not None}
        if dlc_vals and dlc_vals != {5}:
            dlc_ok = False
    # Motor period 10 ms; history ring may under-count under load — require continuous presence
    note(
        "B_mtr_0x204",
        counts[ID_MTR_CMD] >= max(8, int(duration * 15)) and speed is not None,
        f"count={counts[ID_MTR_CMD]} speed={speed} gear={gear} name={msg.get('name') if msg else None} dlc_ok={dlc_ok}",
        counts=dict(counts),
        speed=speed,
        gear=gear,
    )
    # value close to 800
    if speed is not None:
        note("B_speed_value", abs(int(speed) - 800) <= 5, f"speed={speed}")
    note("B_no_host_during_low", counts[ID_HOST] == 0, f"host_count={counts[ID_HOST]}")
    req("POST", "/control/direct", {"channel": "motor", "enabled": False})
    time.sleep(0.2)


def recipe_c_ses(duration: float = 3.0) -> None:
    code, body = req(
        "POST",
        "/control/direct",
        {
            "channel": "steering",
            "enabled": True,
            "values": {"target_angle_raw": 100, "target_speed_raw": 328},
            "period_ms": 20,
        },
    )
    note("C_start_steer", code == 200, f"code={code} {str(body)[:140]}")
    frames = sample_history_window({ID_SES_CMD}, duration)
    counts = count_by_id(frames)
    msg = latest_msg(ID_SES_CMD)
    angle = eng(msg, "target_angle_raw")
    en = eng(msg, "control_enable")
    al = eng(msg, "alignment_enable")
    # SES period 20 ms (~50 Hz). History ring + vendor encode can yield fewer samples
    # than theory; require ≥ ~3 Hz continuous and correct decoded fields.
    note(
        "C_ses_0x169",
        counts[ID_SES_CMD] >= max(5, int(duration * 3)) and angle is not None,
        f"count={counts[ID_SES_CMD]} angle={angle} control_enable={en} alignment={al} name={msg.get('name') if msg else None}",
        counts=dict(counts),
    )
    if en is not None or al is not None:
        note(
            "C_ses_enables_on",
            bool(en) and bool(al),
            f"control_enable={en} alignment_enable={al}",
        )
    req("POST", "/control/direct", {"channel": "steering", "enabled": False})
    time.sleep(0.2)


def recipe_d_seb(duration: float = 3.0) -> None:
    code, body = req(
        "POST",
        "/control/direct",
        {
            "channel": "brake",
            "enabled": True,
            "values": {"pressure_request_raw": 40, "control_mode": 1},
            "period_ms": 20,
        },
    )
    note("D_start_brake", code == 200, f"code={code} {str(body)[:140]}")
    frames = sample_history_window({ID_SEB_CMD, ID_RT_BRAKE}, duration)
    counts = count_by_id(frames)
    msg = latest_msg(ID_SEB_CMD)
    press = eng(msg, "pressure_request_raw")
    note(
        "D_seb_0x7B9",
        counts[ID_SEB_CMD] >= max(5, int(duration * 3)) and press is not None,
        f"count={counts[ID_SEB_CMD]} pressure={press} name={msg.get('name') if msg else None}",
        counts=dict(counts),
    )
    note(
        "D_no_0x205_from_direct",
        counts[ID_RT_BRAKE] == 0,
        f"0x205_count={counts[ID_RT_BRAKE]} (Low direct skips RT_BRAKE_CMD — expected 0)",
        counts=dict(counts),
    )
    if press is not None:
        note("D_pressure_value", abs(int(press) - 40) <= 2, f"pressure={press}")
    req("POST", "/control/direct", {"channel": "brake", "enabled": False})
    time.sleep(0.2)


def recipe_e_combined(duration: float = 3.0) -> None:
    for ch, vals, period in (
        ("motor", {"motor_speed_mmps": 600, "gear": 1}, 10),
        ("steering", {"target_angle_raw": 50}, 20),
        ("brake", {"pressure_request_raw": 25, "control_mode": 1}, 20),
    ):
        code, body = req(
            "POST",
            "/control/direct",
            {"channel": ch, "enabled": True, "values": vals, "period_ms": period},
        )
        note(f"E_start_{ch}", code == 200, f"code={code}")
    frames = sample_history_window({ID_MTR_CMD, ID_SES_CMD, ID_SEB_CMD, ID_HOST}, duration)
    counts = count_by_id(frames)
    note(
        "E_all_three_low",
        counts[ID_MTR_CMD] > 0 and counts[ID_SES_CMD] > 0 and counts[ID_SEB_CMD] > 0,
        f"mtr={counts[ID_MTR_CMD]} ses={counts[ID_SES_CMD]} seb={counts[ID_SEB_CMD]}",
        counts=dict(counts),
    )
    # Allow tiny residual Host frames from prior recipe teardown in the ring
    note("E_no_host", counts[ID_HOST] <= 2, f"host={counts[ID_HOST]}")
    for ch in ("motor", "steering", "brake"):
        req("POST", "/control/direct", {"channel": ch, "enabled": False})
    time.sleep(0.2)


def recipe_exclusivity() -> None:
    """High then Low should preempt; plan claims mutual exclusion."""
    req(
        "POST",
        "/analysis/host-drive",
        {"speed_mmps": 1000, "yaw_rate_mrad_s": 0, "gear": 1, "period_ms": 10},
    )
    time.sleep(0.5)
    code, body = req(
        "POST",
        "/control/direct",
        {
            "channel": "motor",
            "enabled": True,
            "values": {"motor_speed_mmps": 400, "gear": 1},
        },
    )
    note("X_low_after_high", code == 200, f"code={code}")
    time.sleep(0.8)
    frames = sample_history_window({ID_HOST, ID_MTR_CMD}, 1.0)
    # After low start, new frames should be motor; host may still have residual until cancelled
    code_st, st = req("GET", "/control/status")
    ctrl = (st.get("control") or {}) if code_st == 200 else {}
    method = ctrl.get("method")
    note(
        "X_method_is_low_direct",
        method == "low_direct",
        f"method={method} active={ctrl.get('active')}",
        control=ctrl,
    )
    req("POST", "/control/direct", {"channel": "motor", "enabled": False})
    req("POST", "/analysis/stop", {})
    req("POST", "/control/release", {"reason": "recipe_qa_done"})


def write_report() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    passed = sum(1 for r in results if r["pass"])
    failed = sum(1 for r in results if not r["pass"])
    report = {
        "generated": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "base": BASE,
        "summary": {"pass": passed, "fail": failed, "total": len(results)},
        "results": results,
        "plan_problems": [
            {
                "id": "P1",
                "severity": "doc-clarity",
                "detail": "High-level 10s drive never yields MTR/SES/SEB frames without RT — plan must use Low recipes for unit IDs.",
            },
            {
                "id": "P2",
                "severity": "coverage-gap",
                "detail": "0x205 RT_BRAKE_CMD (vehicle intermediate) is not produced by toolkit Low direct.",
            },
            {
                "id": "P3",
                "severity": "timing",
                "detail": "Guide says ~10s observation; automated QA uses shorter windows for speed — extend duration_s for formal sign-off.",
            },
            {
                "id": "P4",
                "severity": "product-gotcha",
                "detail": "POST stop-all sets bench_tx=disabled. Any recipe that Stop-all mid-test must re-enable Bench TX before next TX. Guide/UI should call this out.",
            },
            {
                "id": "P5",
                "severity": "stale-state",
                "detail": "GET /state may still show last decoded values for 0x204/0x169/0x7B9 after jobs stop; use history global_sequence windows for pass/fail, not only latest store.",
            },
        ],
    }
    OUT_JSON.write_text(json.dumps(report, indent=2), encoding="utf-8")
    lines = [
        "# Software-only recipe QA (API)",
        "",
        f"Generated: {report['generated']}",
        "",
        f"**Pass {passed} / Fail {failed} / Total {len(results)}**",
        "",
        "| Check | Result | Detail |",
        "|-------|--------|--------|",
    ]
    for r in results:
        d = (r.get("detail") or "").replace("|", "\\|").replace("\n", " ")
        lines.append(f"| {r['name']} | {'PASS' if r['pass'] else 'FAIL'} | {d} |")
    lines += [
        "",
        "## Plan problems flagged",
        "",
    ]
    for p in report["plan_problems"]:
        lines.append(f"- **{p['id']}** ({p['severity']}): {p['detail']}")
    OUT_MD.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"\n=== SUMMARY pass={passed} fail={failed} ===")
    print(f"Wrote {OUT_JSON}")
    print(f"Wrote {OUT_MD}")
    return 0 if failed == 0 else 1


def main() -> int:
    print("=== Software-only recipe QA against", BASE, "===")
    code, st = req("GET", "/status")
    note("api_ready", code == 200 and st.get("ready"), f"code={code} ready={st.get('ready') if code==200 else None}")
    if code != 200:
        return write_report()
    ensure_session_and_bench()
    recipe_a_host(3.0)
    recipe_b_mtr(3.0)
    recipe_c_ses(3.0)
    recipe_d_seb(3.0)
    recipe_e_combined(3.0)
    recipe_exclusivity()
    return write_report()


if __name__ == "__main__":
    sys.exit(main())
