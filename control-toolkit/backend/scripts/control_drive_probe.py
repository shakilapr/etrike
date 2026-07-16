"""Probe Control + Drive backend command paths and bus reactions."""

from __future__ import annotations

import json
import time
import urllib.error
import urllib.request
from pathlib import Path

BASE = "http://127.0.0.1:8001/api/v1"
OUT = Path(__file__).resolve().parents[2] / "test-results" / "control-drive-probe.json"
results: list[dict] = []


def req(method: str, path: str, body: dict | None = None):
    data = None if body is None else json.dumps(body).encode()
    headers = {"Content-Type": "application/json"} if data else {}
    r = urllib.request.Request(BASE + path, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(r, timeout=8) as resp:
            raw = resp.read().decode()
            return resp.status, json.loads(raw) if raw else {}
    except urllib.error.HTTPError as e:
        raw = e.read().decode()
        try:
            j = json.loads(raw)
        except Exception:
            j = {"raw": raw[:400]}
        return e.code, j
    except Exception as e:  # noqa: BLE001
        return 0, {"error": str(e)}


def ok(name: str, cond: bool, detail: str = "") -> None:
    results.append({"name": name, "pass": bool(cond), "detail": detail})
    print(("PASS" if cond else "FAIL"), name, (detail or "")[:180])


def msg_by(msgs: list, *, name: str | None = None, can_id: int | None = None):
    for m in msgs:
        if name and m.get("name") == name:
            return m
        if can_id is not None and int(m.get("can_id") or 0) == can_id:
            return m
    return None


def eng(m, key: str):
    if not m:
        return None
    s = (m.get("signals") or {}).get(key) or {}
    return s.get("engineering_value")


def main() -> int:
    code, st = req("GET", "/status")
    ok("status ready", code == 200 and st.get("ready"), f"code={code}")

    code, sess = req(
        "POST",
        "/sessions",
        {"profile": "pure_software", "destination": "virtual"},
    )
    sid = None
    if isinstance(sess, dict):
        sid = (sess.get("session") or {}).get("session_id") or sess.get("session_id")
    if not sid:
        code, st2 = req("GET", "/status")
        sid = (st2.get("session") or {}).get("session_id")
    ok("session id", bool(sid), f"sid={sid} sess_keys={list(sess) if isinstance(sess, dict) else sess}")

    code, bt = req("POST", f"/sessions/{sid}/bench-tx", {"enabled": True})
    ok("bench_tx enable", code == 200, f"code={code}")

    # ── HIGH kinematics ──────────────────────────────────────────────
    code, r = req(
        "POST",
        "/control/intent",
        {
            "sequence": 1,
            "source": "probe",
            "mode": "kinematics",
            "throttle": 0.5,
            "steer": 0.2,
            "gear": 1,
            "hard_brake": False,
        },
    )
    snap = r.get("control") or {}
    ok(
        "intent high_kinematics",
        code == 200 and snap.get("method") == "high_kinematics",
        f"code={code} method={snap.get('method')} speed={snap.get('shaped_speed_mmps')} yaw={snap.get('shaped_yaw_mrad_s')} gear={snap.get('gear')}",
    )
    ok(
        "intent shaped speed ~1500",
        abs((snap.get("shaped_speed_mmps") or 0) - 1500) < 50,
        str(snap.get("shaped_speed_mmps")),
    )
    ok(
        "intent shaped yaw ~600",
        abs((snap.get("shaped_yaw_mrad_s") or 0) - 600) < 50,
        str(snap.get("shaped_yaw_mrad_s")),
    )

    time.sleep(0.2)
    code, state = req("GET", "/state")
    msgs = state.get("messages") or []
    host = msg_by(msgs, name="HOST_DRIVE_CMD") or msg_by(msgs, can_id=0x300)
    ok("HOST_DRIVE_CMD on bus", host is not None, f"n={len(msgs)}")
    if host:
        ok("HOST speed_mmps present", eng(host, "speed_mmps") is not None, str(eng(host, "speed_mmps")))
        ok("HOST gear present", eng(host, "gear") is not None or (host.get("signals") or {}).get("gear"), list((host.get("signals") or {}).keys()))

    code, _ = req(
        "POST",
        "/control/intent",
        {"sequence": 0, "source": "probe", "mode": "kinematics", "throttle": 0.1},
    )
    ok("stale sequence 409", code == 409, f"code={code}")

    code, rel = req("POST", "/control/release", {"reason": "probe"})
    ok(
        "release clears active",
        code == 200 and not (rel.get("control") or {}).get("active"),
        str((rel.get("control") or {}).get("loss_reason")),
    )

    # ── LOW direct motor ─────────────────────────────────────────────
    code, d = req(
        "POST",
        "/control/direct",
        {
            "channel": "motor",
            "enabled": True,
            "values": {"motor_speed_mmps": 800, "gear": 1},
        },
    )
    snap = d.get("control") or {}
    ok(
        "direct motor start",
        code == 200 and "motor" in (snap.get("direct_channels") or []),
        f"method={snap.get('method')} ch={snap.get('direct_channels')}",
    )
    time.sleep(0.15)
    code, state = req("GET", "/state")
    msgs = state.get("messages") or []
    rt = msg_by(msgs, name="RT_DRIVE_CMD") or msg_by(msgs, can_id=0x204)
    ok("RT_DRIVE_CMD 0x204 present", rt is not None, [m.get("name") for m in msgs if m.get("bus") == "low"][:12])
    if rt:
        ok(
            "RT motor_speed ~800",
            abs((eng(rt, "motor_speed_mmps") or 0) - 800) < 50,
            str(eng(rt, "motor_speed_mmps")),
        )

    # ── LOW steer ────────────────────────────────────────────────────
    code, d = req(
        "POST",
        "/control/direct",
        {
            "channel": "steering",
            "enabled": True,
            "values": {"target_angle_raw": 100, "target_speed_raw": 200},
        },
    )
    snap = d.get("control") or {}
    ok(
        "direct steer start",
        code == 200 and "steering" in (snap.get("direct_channels") or []),
        str(snap.get("direct_channels")),
    )
    time.sleep(0.2)
    code, state = req("GET", "/state")
    msgs = state.get("messages") or []
    ses_req = msg_by(msgs, name="VCU_SES_REQ") or msg_by(msgs, can_id=0x169)
    ok("VCU_SES_REQ 0x169 present", ses_req is not None)
    if ses_req:
        sigs = ses_req.get("signals") or {}
        ce = eng(ses_req, "control_enable")
        ae = eng(ses_req, "alignment_enable")
        ok("steer control_enable ON", ce in (True, 1), f"ce={ce} keys={list(sigs.keys())}")
        ok("steer alignment_enable ON", ae in (True, 1), f"ae={ae}")
        ok(
            "steer angle raw ~100",
            abs((eng(ses_req, "target_angle_raw") or 0) - 100) < 5,
            str(eng(ses_req, "target_angle_raw")),
        )

    # ── LOW brake ────────────────────────────────────────────────────
    code, d = req(
        "POST",
        "/control/direct",
        {
            "channel": "brake",
            "enabled": True,
            "values": {"pressure_request_raw": 40, "control_mode": 1},
        },
    )
    ok(
        "direct brake start",
        code == 200 and "brake" in ((d.get("control") or {}).get("direct_channels") or []),
        str((d.get("control") or {}).get("direct_channels")),
    )
    time.sleep(0.2)
    code, state = req("GET", "/state")
    msgs = state.get("messages") or []
    seb = msg_by(msgs, name="VCU_SEB_REQ") or msg_by(msgs, can_id=0x7B9)
    ok("VCU_SEB_REQ 0x7B9 present", seb is not None)
    if seb:
        ce = eng(seb, "control_enable")
        ok("brake control_enable ON", ce in (True, 1), f"ce={ce}")
        ok(
            "brake pressure ~40",
            abs((eng(seb, "pressure_request_raw") or 0) - 40) < 5,
            str(eng(seb, "pressure_request_raw")),
        )

    # ── mutual exclusion ─────────────────────────────────────────────
    code, r = req(
        "POST",
        "/control/intent",
        {
            "sequence": 10,
            "source": "probe",
            "mode": "kinematics",
            "throttle": 0.1,
            "steer": 0,
            "gear": 1,
        },
    )
    snap = r.get("control") or {}
    ok(
        "high preempts low",
        code == 200
        and snap.get("method") == "high_kinematics"
        and not snap.get("direct_channels"),
        f"method={snap.get('method')} direct={snap.get('direct_channels')}",
    )

    code, d = req(
        "POST",
        "/control/direct",
        {
            "channel": "motor",
            "enabled": True,
            "values": {"motor_speed_mmps": 100, "gear": 1},
        },
    )
    snap = d.get("control") or {}
    ok(
        "low preempts high",
        code == 200 and snap.get("method") == "low_direct" and not snap.get("job_id"),
        f"method={snap.get('method')} job={snap.get('job_id')}",
    )

    # ── HMI ──────────────────────────────────────────────────────────
    code, h = req("POST", "/hmi/mode", {"req_mode": 0, "enabled": True})
    ok("hmi mode MANUAL", code == 200 and h.get("requested_mode") == "MANUAL", str(h)[:120])
    code, h = req("POST", "/hmi/power", {"req_start": 1, "enabled": True})
    ok("hmi power ON", code == 200 and h.get("requested_power") == "ON", str(h)[:120])
    time.sleep(0.25)
    code, st = req("GET", "/status")
    ses = st.get("session") or {}
    ok("status requested_mode MANUAL", ses.get("requested_mode") == "MANUAL", str(ses.get("requested_mode")))
    ok("status requested_power ON", ses.get("requested_power") == "ON", str(ses.get("requested_power")))
    code, state = req("GET", "/state")
    msgs = state.get("messages") or []
    mode_msg = msg_by(msgs, name="HMI_MODE_REQ")
    pwr_msg = msg_by(msgs, name="HMI_PWR_REQ")
    ok("HMI_MODE_REQ on bus", mode_msg is not None)
    ok("HMI_PWR_REQ on bus", pwr_msg is not None)

    # ── oneshot inject (analysis path after releasing motion ownership) ─
    req("POST", "/control/release", {"reason": "pre_inject"})
    code, inj = req(
        "POST",
        "/analysis/host-drive",
        {
            "speed_mmps": 550,
            "yaw_rate_mrad_s": 120,
            "gear": 1,
        },
    )
    ok(
        "analysis host-drive oneshot",
        code in (200, 201) and (inj.get("ok") or inj.get("mode") == "oneshot"),
        f"code={code} body={str(inj)[:140]}",
    )

    # ── bench gate ───────────────────────────────────────────────────
    req("POST", f"/sessions/{sid}/bench-tx", {"enabled": False})
    code, bad = req(
        "POST",
        "/control/intent",
        {"sequence": 99, "throttle": 0.2, "mode": "kinematics"},
    )
    ok("intent blocked without bench_tx", code in (409, 403, 400), f"code={code}")

    req("POST", f"/sessions/{sid}/bench-tx", {"enabled": True})
    req("POST", "/control/release", {"reason": "probe_done"})

    fails = [x for x in results if not x["pass"]]
    summary = {
        "total": len(results),
        "pass": len(results) - len(fails),
        "fail": len(fails),
        "results": results,
    }
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print("--- SUMMARY ---")
    print(f"{summary['pass']}/{summary['total']} pass, {summary['fail']} fail → {OUT}")
    for f in fails:
        print(" FAIL", f["name"], f["detail"][:200])
    return 1 if fails else 0


if __name__ == "__main__":
    raise SystemExit(main())
