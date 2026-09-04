#!/usr/bin/env python3
"""Low-level command-chain QA — Host speed+gear → RT → Low actuator signals.

Runs against a LIVE bench backend (default http://127.0.0.1:8001). Verifies
that Host commands on the High bus produce (or deliberately withhold) RT's
Low-bus actuator frames across modes:

  A. Command-source equivalence (AUTO → non-zero Low frames)
  B. Mode gating (MANUAL pins 0x204 to {0,N}; AUTO drives; ESTOP stops)
  C. Signal integrity / unit routing
  D. Ownership / exclusivity handoffs

Bench precondition: RT running hardware_bench (GPIO42 grounded), SYS on the
Low bus (GPIO1 grounded), bench_test session + Bench TX enabled, ESTOP clear.

Artifacts are written to control-toolkit/test-results/api-qa/.

Usage:
  python scripts/low_level_chain_qa.py [--base http://127.0.0.1:8001]
      [--cases A1,B9,...] [--stop-on-fail] [--out DIR]
"""

from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from collections import Counter
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]  # control-toolkit/
OUT_DIR = ROOT / "test-results" / "api-qa"

ID_HOST = 0x300
ID_MTR_CMD = 0x204
ID_SES_CMD = 0x169
ID_SEB_CMD = 0x7B9
ID_RT_BRAKE = 0x205
ID_SYS_MODE = 0x110
ID_SYS_HB = 0x7FE
ID_RT_HB = 0x7FD
ID_SAFETY_ESTOP = 0x001

MODE_MANUAL = "MANUAL"
MODE_AUTO = "AUTO"
MODE_ESTOP = "ESTOP"

SES_CENTER = 30000  # steer-by-wire CSV offset: 0 deg → raw 30000


@dataclass
class Check:
    id: str
    case: str
    ok: bool
    detail: str
    expected: Any = None
    observed: Any = None
    error: str | None = None
    duration_ms: float = 0.0


@dataclass
class Report:
    started: str
    base_url: str
    checks: list[Check] = field(default_factory=list)

    def add(self, c: Check) -> None:
        self.checks.append(c)
        print(f"  [{'PASS' if c.ok else 'FAIL'}] {c.id} — {c.detail}")

    @property
    def failed(self) -> list[Check]:
        return [c for c in self.checks if not c.ok]

    @property
    def passed(self) -> list[Check]:
        return [c for c in self.checks if c.ok]


class Api:
    def __init__(self, base: str, timeout: float = 10.0) -> None:
        self.base = base.rstrip("/")

    def j(self, method, path, json_body=None, expect=None, timeout=10.0):
        data = None if json_body is None else json.dumps(json_body).encode()
        headers = {"Content-Type": "application/json"} if data else {}
        r = urllib.request.Request(
            self.base + path, data=data, method=method, headers=headers
        )
        try:
            with urllib.request.urlopen(r, timeout=timeout) as resp:
                raw = resp.read().decode()
                code = resp.status
                body = json.loads(raw) if raw else {}
        except urllib.error.HTTPError as e:
            raw = e.read().decode()
            code = e.code
            try:
                body = json.loads(raw)
            except Exception:
                body = {"_raw": raw[:500]}
        except Exception as exc:  # noqa: BLE001
            code = 0
            body = {"_raw": str(exc)[:300]}
        if expect is not None:
            allowed = {expect} if isinstance(expect, int) else set(expect)
            if code not in allowed:
                raise AssertionError(f"{method} {path} -> {code} expected {allowed}: {body}")
        return code, body

    def status(self) -> dict:
        _, b = self.j("GET", "/status")
        return b

    def state(self) -> list[dict]:
        _, b = self.j("GET", "/state")
        return b.get("messages") or []

    def history(self, limit: int = 800) -> list[dict]:
        _, b = self.j("GET", f"/history?limit={limit}")
        return b.get("frames") or []

    def intent(self, sequence, source="qa", throttle=0.0, steer=0.0, gear=1,
               hard_brake=False, estop=False):
        body = {
            "sequence": sequence,
            "source": source,
            "mode": "kinematics",
            "throttle": throttle,
            "steer": steer,
            "gear": gear,
            "hard_brake": hard_brake,
            "estop": estop,
        }
        code, resp = self.j("POST", "/control/intent", body, expect={200, 409, 422})
        return code, resp.get("control") or {}

    def direct(self, channel, enabled=True, values=None, period_ms=None):
        body = {"channel": channel, "enabled": enabled, "values": values or {}}
        if period_ms is not None:
            body["period_ms"] = period_ms
        code, resp = self.j("POST", "/control/direct", body, expect={200, 400, 409, 422})
        return code, resp.get("control") or {}

    def release(self, reason="qa"):
        return self.j("POST", "/control/release", {"reason": reason})

    def hmi_mode(self, req_mode, enabled=True):
        return self.j("POST", "/hmi/mode", {"req_mode": req_mode, "enabled": enabled})

    def inject(self, bus, key, values, owner="qa", period_ms=None):
        body = {"bus": bus, "key": key, "values": values, "owner": owner}
        if period_ms is not None:
            body["period_ms"] = period_ms
        code, resp = self.j("POST", "/injections", body, expect={200, 400, 409, 422})
        return code, resp

    def inject_stop_all(self):
        self.j("DELETE", "/injections", {})


def eng(msg: dict | None, key: str):
    if not msg:
        return None
    s = (msg.get("signals") or {}).get(key) or {}
    return s.get("engineering_value")


def num(msg: dict | None, key: str):
    v = eng(msg, key)
    if v is None:
        return None
    try:
        return int(v)
    except (TypeError, ValueError):
        try:
            return int(float(v))
        except (TypeError, ValueError):
            return None


def fresh_ok(msg: dict | None) -> bool:
    if not msg:
        return False
    return str(msg.get("freshness") or "").lower() in ("live", "late")


def find_msg(msgs, can_id=None, name=None, bus=None):
    for m in msgs:
        if can_id is not None and int(m.get("can_id") or -1) != can_id:
            continue
        if name and m.get("name") != name:
            continue
        if bus and m.get("bus") != bus:
            continue
        return m
    return None


def wait_state(api, *, can_id=None, name=None, bus=None, timeout_s=3.0):
    deadline = time.time() + timeout_s
    last = None
    while time.time() < deadline:
        m = find_msg(api.state(), can_id=can_id, name=name, bus=bus)
        if m is not None:
            return m
        time.sleep(0.05)
    return last


def wait_mode(api, mode: str, timeout_s=5.0) -> bool:
    """Wait until authoritative vehicle mode (RT_STATE_RPT / SYS_MODE_CMD) == mode."""
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        msgs = api.state()
        rt = find_msg(msgs, name="RT_STATE_RPT")
        if fresh_ok(rt) and _mode_of(rt) == mode:
            return True
        sm = find_msg(msgs, can_id=ID_SYS_MODE)
        if fresh_ok(sm) and _mode_of(sm) == mode:
            return True
        time.sleep(0.1)
    return False


def _mode_of(msg: dict | None) -> str | None:
    """Return MANUAL/AUTO/ESTOP from a message's mode signal."""
    if not msg:
        return None
    raw = eng(msg, "mode")
    if raw is None:
        return None
    if isinstance(raw, str) and raw:
        u = raw.upper()
        if u in ("MANUAL", "AUTO", "ESTOP"):
            return u
    n = num(msg, "mode")
    return {0: "MANUAL", 1: "AUTO", 2: "ESTOP"}.get(n if n is not None else -1)


def wait_hb_ok(api, timeout_s=4.0) -> bool:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        hb = find_msg(api.state(), can_id=ID_SYS_HB, bus="low") or find_msg(
            api.state(), can_id=ID_SYS_HB
        )
        if hb is not None and fresh_ok(hb) and num(hb, "heartbeat_ok") == 1:
            return True
        time.sleep(0.1)
    return False


class BenchContext:
    """Ensures a bench_test session + Bench TX enabled; restores state on teardown."""

    def __init__(self, api: Api) -> None:
        self.api = api

    def ensure_session_and_tx(self) -> None:
        st = self.api.status()
        ses = st.get("session") or {}
        sid = ses.get("session_id")
        profile = ses.get("profile")
        if not sid or profile != "bench_test":
            if sid:
                self.api.j(
                    "DELETE", f"/sessions/{sid}",
                    {"expected_revision": ses.get("revision", 0)},
                    expect={200, 409, 404},
                )
            _, created = self.api.j("POST", "/sessions", {"profile": "bench_test"})
            sid = created["session"]["session_id"]
        if (ses.get("bench_tx") or "") != "enabled":
            st = self.api.status()
            ses = st.get("session") or {}
            self.api.j(
                "POST", f"/sessions/{sid}/bench-tx",
                {"enabled": True, "expected_revision": ses.get("revision", 0)},
                expect={200, 409},
            )
        self.api.inject_stop_all()
        self.api.release("llc_setup")

    def set_mode(self, mode: str, wait=True) -> bool:
        req_mode = 1 if mode == MODE_AUTO else 0
        code, _ = self.api.hmi_mode(req_mode, True)
        if code != 200:
            return False
        if wait:
            return wait_mode(self.api, mode, timeout_s=5.0)
        return True

    def teardown(self) -> None:
        self.api.inject_stop_all()
        self.api.release("llc_teardown")
        self.api.hmi_mode(1, False)  # cancel HMI job


def run_case_a(api, ctx: BenchContext, report: Report, only=None) -> None:
    """A. Command-source equivalence in AUTO — non-zero Low frames expected."""

    def do(case_id, fn):
        if only and case_id not in only:
            return
        t0 = time.perf_counter()
        try:
            fn(case_id, t0)
        except Exception as exc:  # noqa: BLE001
            report.add(Check(case_id, "A", False, "failed", error=str(exc)))
            ctx.teardown()

    def base(case_id, t0, throttle=0.6, steer=0.0, gear=1):
        if not ctx.set_mode(MODE_AUTO):
            report.add(Check(case_id, "A", False, "could not set AUTO",
                             duration_ms=(time.perf_counter() - t0) * 1000))
            return False
        return True

    def a1(case_id, t0):
        if not base(case_id, t0): return
        code, ctrl = api.intent(1, source="qa_a1", throttle=0.6, steer=0.0, gear=1)
        time.sleep(0.5)
        m = wait_state(api, can_id=ID_MTR_CMD, bus="low", timeout_s=2.0)
        spd = num(m, "motor_speed_mmps") if m else None
        gr = num(m, "gear") if m else None
        ok = code == 200 and m is not None and spd is not None and abs(spd - 1800) <= 60 and gr == 1
        report.add(Check(case_id, "A", ok,
                         f"intent->0x204 speed={spd} gear={gr}",
                         expected={"speed≈1800", "gear=1"}, observed={"speed": spd, "gear": gr},
                         duration_ms=(time.perf_counter() - t0) * 1000))
        api.release("a1")

    def a2(case_id, t0):
        if not base(case_id, t0): return
        code, ctrl = api.intent(1, source="qa_a2", throttle=-0.3, steer=0.0, gear=1)
        time.sleep(0.5)
        m = wait_state(api, can_id=ID_MTR_CMD, bus="low", timeout_s=2.0)
        spd = num(m, "motor_speed_mmps") if m else None
        gr = num(m, "gear") if m else None
        # reverse intent: negative, bounded by -500; gear should resolve to R(3)
        ok = code == 200 and m is not None and spd is not None and spd < 0 and gr in (3, None)
        report.add(Check(case_id, "A", ok,
                         f"reverse->0x204 speed={spd} gear={gr}",
                         expected={"speed<0", "gear=R(3)"}, observed={"speed": spd, "gear": gr},
                         duration_ms=(time.perf_counter() - t0) * 1000))
        api.release("a2")

    def a3(case_id, t0):
        if not base(case_id, t0): return
        code, resp = api.j("POST", "/analysis/host-drive",
                           {"speed_mmps": 1500, "yaw_rate_mrad_s": 0, "gear": 1,
                            "period_ms": 20}, expect={200, 201, 400, 409, 422})
        time.sleep(0.6)
        m = wait_state(api, can_id=ID_MTR_CMD, bus="low", timeout_s=2.0)
        spd = num(m, "motor_speed_mmps") if m else None
        gr = num(m, "gear") if m else None
        ok = code in (200, 201) and m is not None and spd is not None and abs(spd - 1500) <= 60 and gr == 1
        report.add(Check(case_id, "A", ok,
                         f"analysis->0x204 speed={spd} gear={gr}",
                         expected={"speed=1500", "gear=1"}, observed={"speed": spd, "gear": gr},
                         duration_ms=(time.perf_counter() - t0) * 1000))
        api.j("POST", "/analysis/stop", {})

    def a4(case_id, t0):
        if not base(case_id, t0): return
        code, resp = api.inject("high", "host:host_drive_cmd",
                                {"speed_mmps": 1500, "yaw_rate_mrad_s": 0, "gear": 1},
                                owner="qa_a4", period_ms=20)
        time.sleep(0.6)
        m = wait_state(api, can_id=ID_MTR_CMD, bus="low", timeout_s=2.0)
        spd = num(m, "motor_speed_mmps") if m else None
        gr = num(m, "gear") if m else None
        ok = code == 200 and m is not None and spd is not None and abs(spd - 1500) <= 60 and gr == 1
        report.add(Check(case_id, "A", ok,
                         f"0x300 inject->0x204 speed={spd} gear={gr}",
                         expected={"speed=1500", "gear=1"}, observed={"speed": spd, "gear": gr},
                         duration_ms=(time.perf_counter() - t0) * 1000))
        api.inject_stop_all()

    def a5(case_id, t0):
        if not base(case_id, t0): return
        code, _ = api.intent(1, source="qa_a5", throttle=0.3, steer=0.2, gear=1)
        time.sleep(0.6)
        m = wait_state(api, can_id=ID_SES_CMD, bus="low", timeout_s=2.0)
        ang = num(m, "target_angle_raw") if m else None
        en = num(m, "control_enable") if m else None
        ok = code == 200 and m is not None and ang is not None and ang > SES_CENTER and en == 1
        report.add(Check(case_id, "A", ok,
                         f"steer+ ->0x169 angle={ang} center={SES_CENTER} enable={en}",
                         expected={"angle>center", "control_enable=1"},
                         observed={"angle": ang, "enable": en},
                         duration_ms=(time.perf_counter() - t0) * 1000))
        api.release("a5")

    def a6(case_id, t0):
        if not base(case_id, t0): return
        code, _ = api.intent(1, source="qa_a6", throttle=0.3, steer=-0.2, gear=1)
        time.sleep(0.6)
        m = wait_state(api, can_id=ID_SES_CMD, bus="low", timeout_s=2.0)
        ang = num(m, "target_angle_raw") if m else None
        en = num(m, "control_enable") if m else None
        ok = code == 200 and m is not None and ang is not None and ang < SES_CENTER and en == 1
        report.add(Check(case_id, "A", ok,
                         f"steer- ->0x169 angle={ang} center={SES_CENTER} enable={en}",
                         expected={"angle<center", "control_enable=1"},
                         observed={"angle": ang, "enable": en},
                         duration_ms=(time.perf_counter() - t0) * 1000))
        api.release("a6")

    def a7(case_id, t0):
        if not base(case_id, t0): return
        code, _ = api.intent(1, source="qa_a7", throttle=0.5, steer=0.0, gear=1, hard_brake=True)
        time.sleep(0.5)
        m = wait_state(api, can_id=ID_MTR_CMD, bus="low", timeout_s=2.0)
        spd = num(m, "motor_speed_mmps") if m else None
        gr = num(m, "gear") if m else None
        ok = code == 200 and m is not None and spd == 0 and gr == 0
        report.add(Check(case_id, "A", ok,
                         f"hard_brake->0x204 speed={spd} gear={gr} (expect {0,N})",
                         expected={"speed=0", "gear=N(0)"}, observed={"speed": spd, "gear": gr},
                         duration_ms=(time.perf_counter() - t0) * 1000))
        api.release("a7")

    def a8(case_id, t0):
        if not base(case_id, t0): return
        code, resp = api.inject("high", "host:host_drive_cmd",
                                {"speed_mmps": 1800, "yaw_rate_mrad_s": 0, "gear": 1},
                                owner="qa_a8", period_ms=20)
        time.sleep(0.6)
        m = wait_state(api, can_id=ID_MTR_CMD, bus="low", timeout_s=2.0)
        spd = num(m, "motor_speed_mmps") if m else None
        gr = num(m, "gear") if m else None
        ok = code == 200 and m is not None and spd == 1800 and gr == 1
        report.add(Check(case_id, "A", ok,
                         f"roundtrip 1800/1 ->0x204 speed={spd} gear={gr}",
                         expected={"speed=1800", "gear=1"}, observed={"speed": spd, "gear": gr},
                         duration_ms=(time.perf_counter() - t0) * 1000))
        api.inject_stop_all()

    for cid, fn in [("A1", a1), ("A2", a2), ("A3", a3), ("A4", a4),
                    ("A5", a5), ("A6", a6), ("A7", a7), ("A8", a8)]:
        do(cid, fn)


def run_case_b(api, ctx: BenchContext, report: Report, only=None) -> None:
    """B. Mode gating — MANUAL pins 0x204 to {0,N}; AUTO drives; ESTOP stops."""

    def do(case_id, fn):
        if only and case_id not in only:
            return
        t0 = time.perf_counter()
        try:
            fn(case_id, t0)
        except Exception as exc:  # noqa: BLE001
            report.add(Check(case_id, "B", False, "failed", error=str(exc)))
            ctx.teardown()

    def b9(case_id, t0):
        if not ctx.set_mode(MODE_MANUAL):
            report.add(Check(case_id, "B", False, "could not set MANUAL",
                             duration_ms=(time.perf_counter() - t0) * 1000)); return
        code, resp = api.inject("high", "host:host_drive_cmd",
                                {"speed_mmps": 1500, "yaw_rate_mrad_s": 0, "gear": 1},
                                owner="qa_b9", period_ms=20)
        time.sleep(0.6)
        m = wait_state(api, can_id=ID_MTR_CMD, bus="low", timeout_s=2.0)
        spd = num(m, "motor_speed_mmps") if m else None
        gr = num(m, "gear") if m else None
        ok = code == 200 and m is not None and spd == 0 and gr == 0
        report.add(Check(case_id, "B", ok,
                         f"MANUAL+0x300 spd1500 ->0x204 speed={spd} gear={gr} (expect {0,N}, no motion)",
                         expected={"speed=0", "gear=N(0)"}, observed={"speed": spd, "gear": gr},
                         duration_ms=(time.perf_counter() - t0) * 1000))
        api.inject_stop_all()

    def b10(case_id, t0):
        if not ctx.set_mode(MODE_MANUAL):
            report.add(Check(case_id, "B", False, "could not set MANUAL",
                             duration_ms=(time.perf_counter() - t0) * 1000)); return
        # drive first in MANUAL — expect 0
        code, _ = api.intent(1, source="qa_b10", throttle=0.5, steer=0.0, gear=1)
        time.sleep(0.4)
        m1 = wait_state(api, can_id=ID_MTR_CMD, bus="low", timeout_s=2.0)
        spd1 = num(m1, "motor_speed_mmps") if m1 else None
        # now AUTO
        ctx.set_mode(MODE_AUTO, wait=True)
        time.sleep(0.6)
        m2 = wait_state(api, can_id=ID_MTR_CMD, bus="low", timeout_s=2.0)
        spd2 = num(m2, "motor_speed_mmps") if m2 else None
        ok = spd1 in (0, None) and spd2 is not None and spd2 > 0
        report.add(Check(case_id, "B", ok,
                         f"MANUAL->AUTO 0x204 speed {spd1}->{spd2}",
                         expected={"manual=0", "auto>0"}, observed={"manual": spd1, "auto": spd2},
                         duration_ms=(time.perf_counter() - t0) * 1000))
        api.release("b10")

    def b11(case_id, t0):
        if not ctx.set_mode(MODE_AUTO):
            report.add(Check(case_id, "B", False, "could not set AUTO",
                             duration_ms=(time.perf_counter() - t0) * 1000)); return
        code, _ = api.intent(1, source="qa_b11", throttle=0.5, steer=0.0, gear=1)
        time.sleep(0.5)
        m1 = wait_state(api, can_id=ID_MTR_CMD, bus="low", timeout_s=2.0)
        spd1 = num(m1, "motor_speed_mmps") if m1 else None
        ctx.set_mode(MODE_MANUAL, wait=True)
        time.sleep(0.5)
        m2 = wait_state(api, can_id=ID_MTR_CMD, bus="low", timeout_s=2.0)
        spd2 = num(m2, "motor_speed_mmps") if m2 else None
        ok = spd1 is not None and spd1 > 0 and spd2 == 0
        report.add(Check(case_id, "B", ok,
                         f"AUTO->MANUAL 0x204 speed {spd1}->{spd2}",
                         expected={"auto>0", "manual=0"}, observed={"auto": spd1, "manual": spd2},
                         duration_ms=(time.perf_counter() - t0) * 1000))
        api.release("b11")

    def b12(case_id, t0):
        if not ctx.set_mode(MODE_AUTO):
            report.add(Check(case_id, "B", False, "could not set AUTO",
                             duration_ms=(time.perf_counter() - t0) * 1000)); return
        code, _ = api.intent(1, source="qa_b12", throttle=0.5, steer=0.0, gear=1)
        time.sleep(0.4)
        m1 = wait_state(api, can_id=ID_MTR_CMD, bus="low", timeout_s=2.0)
        spd1 = num(m1, "motor_speed_mmps") if m1 else None
        # dual-bus ESTOP inject
        api.intent(2, source="qa_b12", estop=True)
        time.sleep(0.6)
        m2 = wait_state(api, can_id=ID_MTR_CMD, bus="low", timeout_s=2.0)
        spd2 = num(m2, "motor_speed_mmps") if m2 else None
        gr2 = num(m2, "gear") if m2 else None
        ok = spd1 is not None and spd1 > 0 and spd2 == 0
        report.add(Check(case_id, "B", ok,
                         f"ESTOP during AUTO: 0x204 {spd1}->{spd2} gear={gr2}",
                         expected={"pre>0", "post=0"}, observed={"pre": spd1, "post": spd2},
                         duration_ms=(time.perf_counter() - t0) * 1000))
        api.j("POST", "/control/estop/clear", {})
        # clear ESTOP latch; RT may need a mode re-assert to recover
        ctx.set_mode(MODE_AUTO, wait=True)

    def b13(case_id, t0):
        if not ctx.set_mode(MODE_AUTO):
            report.add(Check(case_id, "B", False, "could not set AUTO",
                             duration_ms=(time.perf_counter() - t0) * 1000)); return
        code, _ = api.intent(1, source="qa_b13", throttle=0.4, steer=0.1, gear=1)
        time.sleep(0.6)
        msgs = api.state()
        rt = find_msg(msgs, name="RT_STATE_RPT")
        steer_state = num(rt, "steer_state") if rt else None
        m = wait_state(api, can_id=ID_MTR_CMD, bus="low", timeout_s=2.0)
        spd = num(m, "motor_speed_mmps") if m else None
        # steer_state 2 == STEER_ACTIVE (see rt steering_control.h)
        ok = steer_state == 2 and spd is not None and spd > 0
        report.add(Check(case_id, "B", ok,
                         f"steer_state={steer_state} (2=ACTIVE) 0x204 speed={spd}",
                         expected={"steer_state=2", "speed>0"},
                         observed={"steer_state": steer_state, "speed": spd},
                         duration_ms=(time.perf_counter() - t0) * 1000))
        api.release("b13")

    for cid, fn in [("B9", b9), ("B10", b10), ("B11", b11), ("B12", b12), ("B13", b13)]:
        do(cid, fn)


def run_case_c(api, ctx: BenchContext, report: Report, only=None) -> None:
    """C. Signal integrity / unit routing."""

    def do(case_id, fn):
        if only and case_id not in only:
            return
        t0 = time.perf_counter()
        try:
            fn(case_id, t0)
        except Exception as exc:  # noqa: BLE001
            report.add(Check(case_id, "C", False, "failed", error=str(exc)))
            ctx.teardown()

    def c14(case_id, t0):
        if not ctx.set_mode(MODE_AUTO):
            report.add(Check(case_id, "C", False, "could not set AUTO",
                             duration_ms=(time.perf_counter() - t0) * 1000)); return
        code, resp = api.inject("high", "host:host_drive_cmd",
                                {"speed_mmps": 1200, "yaw_rate_mrad_s": 0, "gear": 1},
                                owner="qa_c14", period_ms=20)
        time.sleep(0.6)
        low_host = find_msg(api.state(), can_id=ID_HOST, bus="low")
        ok = code == 200 and low_host is None
        report.add(Check(case_id, "C", ok,
                         "0x300 must NOT appear on Low (RT consumes, does not bridge)",
                         expected={"no 0x300 on low"}, observed={"low_0x300": low_host is not None},
                         duration_ms=(time.perf_counter() - t0) * 1000))
        api.inject_stop_all()

    def c15(case_id, t0):
        if not ctx.set_mode(MODE_AUTO):
            report.add(Check(case_id, "C", False, "could not set AUTO",
                             duration_ms=(time.perf_counter() - t0) * 1000)); return
        code, _ = api.intent(1, source="qa_c15", throttle=0.5, steer=0.0, gear=1)
        ok = wait_hb_ok(api, timeout_s=4.0)
        report.add(Check(case_id, "C", ok,
                         "SYS_HEARTBEAT.heartbeat_ok stays 1 during AUTO drive",
                         expected={"hb_ok=1"}, observed={"hb_ok": ok},
                         duration_ms=(time.perf_counter() - t0) * 1000))
        api.release("c15")

    def c16(case_id, t0):
        if not ctx.set_mode(MODE_AUTO):
            report.add(Check(case_id, "C", False, "could not set AUTO",
                             duration_ms=(time.perf_counter() - t0) * 1000)); return
        # D forward
        api.intent(1, source="qa_c16f", throttle=0.4, steer=0.0, gear=1)
        time.sleep(0.4)
        mf = wait_state(api, can_id=ID_MTR_CMD, bus="low", timeout_s=2.0)
        spd_f = num(mf, "motor_speed_mmps") if mf else None
        gr_f = num(mf, "gear") if mf else None
        # R reverse
        api.intent(2, source="qa_c16r", throttle=-0.3, steer=0.0, gear=3)
        time.sleep(0.4)
        mr = wait_state(api, can_id=ID_MTR_CMD, bus="low", timeout_s=2.0)
        spd_r = num(mr, "motor_speed_mmps") if mr else None
        gr_r = num(mr, "gear") if mr else None
        # N neutral
        api.intent(3, source="qa_c16n", throttle=0.0, steer=0.0, gear=0)
        time.sleep(0.4)
        mn = wait_state(api, can_id=ID_MTR_CMD, bus="low", timeout_s=2.0)
        spd_n = num(mn, "motor_speed_mmps") if mn else None
        gr_n = num(mn, "gear") if mn else None
        ok = (spd_f is not None and spd_f > 0 and gr_f == 1) and \
             (spd_r is not None and spd_r < 0 and gr_r == 3) and \
             (spd_n is not None and spd_n == 0 and gr_n == 0)
        report.add(Check(case_id, "C", ok,
                         f"D->({spd_f},{gr_f}) R->({spd_r},{gr_r}) N->({spd_n},{gr_n})",
                         expected={"D>0 g1", "R<0 g3", "N=0 g0"},
                         observed={"D": (spd_f, gr_f), "R": (spd_r, gr_r), "N": (spd_n, gr_n)},
                         duration_ms=(time.perf_counter() - t0) * 1000))
        api.release("c16")

    for cid, fn in [("C14", c14), ("C15", c15), ("C16", c16)]:
        do(cid, fn)


def run_case_d(api, ctx: BenchContext, report: Report, only=None) -> None:
    """D. Ownership / exclusivity handoffs."""

    def do(case_id, fn):
        if only and case_id not in only:
            return
        t0 = time.perf_counter()
        try:
            fn(case_id, t0)
        except Exception as exc:  # noqa: BLE001
            report.add(Check(case_id, "D", False, "failed", error=str(exc)))
            ctx.teardown()

    def d17(case_id, t0):
        if not ctx.set_mode(MODE_AUTO):
            report.add(Check(case_id, "D", False, "could not set AUTO",
                             duration_ms=(time.perf_counter() - t0) * 1000)); return
        # drive_console stream
        for i in range(1, 8):
            api.intent(i, source="drive_console", throttle=0.5, steer=0.0, gear=1)
            time.sleep(0.05)
        # handoff to control_keyboard
        time.sleep(0.2)
        m1 = wait_state(api, can_id=ID_MTR_CMD, bus="low", timeout_s=2.0)
        spd1 = num(m1, "motor_speed_mmps") if m1 else None
        for i in range(1, 8):
            api.intent(i, source="control_keyboard", throttle=0.4, steer=0.0, gear=1)
            time.sleep(0.05)
        time.sleep(0.3)
        m2 = wait_state(api, can_id=ID_MTR_CMD, bus="low", timeout_s=2.0)
        spd2 = num(m2, "motor_speed_mmps") if m2 else None
        # no ESTOP, 0x204 resumed
        st = api.status()
        estop = (st.get("estop") or {}).get("active")
        ok = estop is False and spd1 is not None and spd1 > 0 and spd2 is not None and spd2 > 0
        report.add(Check(case_id, "D", ok,
                         f"handoff drive_console->control_keyboard 0x204 {spd1}->{spd2} estop={estop}",
                         expected={"no estop", "resume>0"}, observed={"spd1": spd1, "spd2": spd2, "estop": estop},
                         duration_ms=(time.perf_counter() - t0) * 1000))
        api.release("d17")

    def d18(case_id, t0):
        # Direct motor bypass works regardless of mode
        ctx.set_mode(MODE_MANUAL, wait=True)
        code, ctrl = api.direct("motor", True, {"motor_speed_mmps": 800, "gear": 1}, period_ms=10)
        time.sleep(0.5)
        m = wait_state(api, can_id=ID_MTR_CMD, bus="low", timeout_s=2.0)
        spd = num(m, "motor_speed_mmps") if m else None
        gr = num(m, "gear") if m else None
        ok = code == 200 and m is not None and spd == 800 and gr == 1
        report.add(Check(case_id, "D", ok,
                         f"direct motor in MANUAL ->0x204 speed={spd} gear={gr}",
                         expected={"speed=800", "gear=1"}, observed={"speed": spd, "gear": gr},
                         duration_ms=(time.perf_counter() - t0) * 1000))
        api.direct("motor", False)

    def d19(case_id, t0):
        ctx.set_mode(MODE_AUTO, wait=True)
        # direct first
        api.direct("motor", True, {"motor_speed_mmps": 600, "gear": 1}, period_ms=10)
        time.sleep(0.3)
        # kinematics preempts direct
        code, ctrl = api.intent(1, source="qa_d19", throttle=0.4, steer=0.0, gear=1)
        time.sleep(0.4)
        ctrl2 = ctrl
        ch = ctrl2.get("direct_channels") or []
        method = ctrl2.get("method")
        ok = code == 200 and method == "high_kinematics" and not ch
        report.add(Check(case_id, "D", ok,
                         f"kinematics preempts direct method={method} direct={ch}",
                         expected={"method=high_kinematics", "direct=[]"},
                         observed={"method": method, "direct": ch},
                         duration_ms=(time.perf_counter() - t0) * 1000))
        api.direct("motor", False)
        api.release("d19")

    def d20(case_id, t0):
        # direct steer/brake bypass forces control_enable ON
        api.direct("steering", True,
                   {"target_angle_raw": 100, "target_speed_raw": 328}, period_ms=20)
        api.direct("brake", True, {"pressure_request_raw": 30, "control_mode": 1}, period_ms=20)
        time.sleep(0.5)
        ms = api.state()
        ses = find_msg(ms, can_id=ID_SES_CMD, bus="low")
        seb = find_msg(ms, can_id=ID_SEB_CMD, bus="low")
        en_ses = num(ses, "control_enable") if ses else None
        en_seb = num(seb, "control_enable") if seb else None
        ang = num(ses, "target_angle_raw") if ses else None
        pr = num(seb, "pressure_request_raw") if seb else None
        ok = ses is not None and seb is not None and en_ses == 1 and en_seb == 1 and \
             ang == 100 and pr == 30
        report.add(Check(case_id, "D", ok,
                         f"direct steer/brake ses_en={en_ses} ang={ang} seb_en={en_seb} pr={pr}",
                         expected={"control_enable=1", "angle=100", "pressure=30"},
                         observed={"ses_en": en_ses, "seb_en": en_seb, "ang": ang, "pr": pr},
                         duration_ms=(time.perf_counter() - t0) * 1000))
        api.direct("steering", False)
        api.direct("brake", False)

    for cid, fn in [("D17", d17), ("D18", d18), ("D19", d19), ("D20", d20)]:
        do(cid, fn)


def main() -> int:
    p = argparse.ArgumentParser(description="Low-level command-chain QA (live bench)")
    p.add_argument("--base", default="http://127.0.0.1:8001")
    p.add_argument("--out", default=str(OUT_DIR))
    p.add_argument("--cases", default="",
                   help="comma list e.g. A1,B9,D18; empty runs all")
    p.add_argument("--stop-on-fail", action="store_true")
    args = p.parse_args()

    only = set(args.cases.split(",")) if args.cases else set()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    report = Report(datetime.now(timezone.utc).isoformat(), args.base)
    api = Api(args.base)

    print(f"Low-level command-chain QA -> {args.base}")
    ctx = BenchContext(api)
    try:
        ctx.ensure_session_and_tx()
    except Exception as exc:  # noqa: BLE001
        print("FATAL: could not establish bench_test session + Bench TX:", exc)
        return 2

    groups = []
    if not only or any(c.startswith("A") for c in only):
        groups.append(lambda: run_case_a(api, ctx, report, only))
    if not only or any(c.startswith("B") for c in only):
        groups.append(lambda: run_case_b(api, ctx, report, only))
    if not only or any(c.startswith("C") for c in only):
        groups.append(lambda: run_case_c(api, ctx, report, only))
    if not only or any(c.startswith("D") for c in only):
        groups.append(lambda: run_case_d(api, ctx, report, only))

    for run in groups:
        run()
        if args.stop_on_fail and report.failed:
            print("Stopping on first failure")
            break

    ctx.teardown()

    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    payload = {
        "started": report.started,
        "base_url": report.base_url,
        "summary": {"total": len(report.checks), "passed": len(report.passed),
                    "failed": len(report.failed)},
        "checks": [__import__("dataclasses").asdict(c) for c in report.checks],
        "failures": [__import__("dataclasses").asdict(c) for c in report.failed],
    }
    for suffix in (f"low_level_chain_qa_{stamp}", "low_level_chain_qa_latest"):
        (out / f"{suffix}.json").write_text(json.dumps(payload, indent=2, default=str), encoding="utf-8")
    lines = [f"# Low-level command-chain QA — {report.started}", "",
             f"**Base:** `{args.base}`", f"**Result:** {len(report.passed)}/{len(report.checks)} passed", ""]
    lines += ["| Status | ID | Detail |", "|---|---|---|"]
    for c in report.checks:
        det = (c.detail or "").replace("|", "/")[:160]
        lines.append(f"| {'PASS' if c.ok else '**FAIL**'} | `{c.id}` | {det} |")
    md = "\n".join(lines) + "\n"
    for suffix in (f"low_level_chain_qa_{stamp}", "low_level_chain_qa_latest"):
        (out / f"{suffix}.md").write_text(md, encoding="utf-8")

    print("\n=== SUMMARY ===")
    print(f"passed {len(report.passed)} / {len(report.checks)}")
    if report.failed:
        print("FAILURES:")
        for c in report.failed:
            print(f"  - {c.id}: {c.detail} | {c.error or ''}")
    return 1 if report.failed else 0


if __name__ == "__main__":
    sys.exit(main())
