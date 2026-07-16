#!/usr/bin/env python3
"""Combination-matrix QA — thousands of high/low control cases.

Runs in-process via FastAPI TestClient (fast, no USB required) unless
--base URL is given (live server, including CANalyst when profile allows).

Dimensions (virtual pure_software):
  - Host kinematics intent: throttle × steer × gear × hard_brake × estop
  - Analysis HOST_DRIVE oneshot: speed × yaw × gear
  - Low direct motor: speed × gear
  - Low direct steering: angle × enables
  - Low direct brake: pressure × enable
  - Multi-step: drive-then-brake / drive-then-estop / reverse-then-brake

Writes reports under test-results/api-qa/matrix_*.json|.md
"""

from __future__ import annotations

import argparse
import itertools
import json
import sys
import time
import traceback
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Iterator

ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT / "test-results" / "api-qa"

# Firmware-aligned (control_intent.py)
MAX_SPEED_FWD = 3000
MAX_SPEED_REV = 500
MAX_YAW = 3000
DEADBAND = 0.05
GEAR_N, GEAR_D, GEAR_S, GEAR_R = 0, 1, 2, 3


def _clamp(x: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, x))


def expected_shape(
    throttle: float,
    steer: float,
    gear: int,
    hard_brake: bool,
) -> tuple[int, int, int]:
    """Mirror ControlIntentService._shape_locked for oracle checks."""
    thr = float(throttle)
    ste = float(steer)
    if abs(thr) < DEADBAND:
        thr = 0.0
    if abs(ste) < DEADBAND:
        ste = 0.0
    if hard_brake:
        thr = 0.0
        g = GEAR_N
    else:
        g = int(gear)
        if thr > 0 and g == GEAR_N:
            g = GEAR_D
        if thr < 0 and g != GEAR_R:
            g = GEAR_R

    if thr >= 0:
        speed = int(round(thr * MAX_SPEED_FWD))
    else:
        speed = int(round(thr * MAX_SPEED_REV))
    if g == GEAR_R and speed > 0:
        speed = -abs(speed)
    if g == GEAR_R and speed == 0 and thr < 0:
        speed = int(round(thr * MAX_SPEED_REV))

    yaw = int(round(ste * MAX_YAW))
    speed = int(_clamp(speed, -MAX_SPEED_REV, MAX_SPEED_FWD))
    yaw = int(_clamp(yaw, -MAX_YAW, MAX_YAW))
    if g == GEAR_S and speed > 0:
        speed = min(int(speed * 1.2), MAX_SPEED_FWD)
    return speed, yaw, g


def as_int(v: Any, default: int = -999999) -> int:
    """int() that preserves 0 (unlike ``x or default`` which treats 0 as missing)."""
    if v is None:
        return default
    try:
        return int(v)
    except (TypeError, ValueError):
        return default


@dataclass
class CaseResult:
    suite: str
    case_id: str
    ok: bool
    detail: str
    inputs: dict[str, Any] = field(default_factory=dict)
    expected: dict[str, Any] = field(default_factory=dict)
    observed: dict[str, Any] = field(default_factory=dict)
    error: str | None = None
    ms: float = 0.0


class Client:
    """Thin wrapper: TestClient or httpx live."""

    def __init__(self, mode: str, base: str | None = None) -> None:
        self.mode = mode
        self._http = None
        self._tc = None
        if mode == "live":
            import httpx

            self._http = httpx.Client(base_url=base or "http://127.0.0.1:8001", timeout=20.0)
        else:
            from fastapi.testclient import TestClient

            from control_toolkit.config import ToolkitConfig
            from control_toolkit.main import create_app

            self._app = create_app(ToolkitConfig())
            self._tc = TestClient(self._app)

    def close(self) -> None:
        if self._http:
            self._http.close()
        if self._tc:
            self._tc.close()

    def request(
        self,
        method: str,
        path: str,
        json_body: Any = None,
    ) -> tuple[int, Any]:
        if self._http is not None:
            r = self._http.request(method, path, json=json_body)
            try:
                body = r.json()
            except Exception:
                body = {"_raw": r.text[:400]}
            return r.status_code, body
        assert self._tc is not None
        r = self._tc.request(method, path, json=json_body)
        try:
            body = r.json()
        except Exception:
            body = {"_raw": r.text[:400]}
        return r.status_code, body


def ensure_bench(c: Client) -> dict[str, Any]:
    code, cur = c.request("GET", "/api/v1/sessions")
    ses = (cur or {}).get("session") or {}
    if ses.get("session_id"):
        c.request(
            "DELETE",
            f"/api/v1/sessions/{ses['session_id']}",
            {"expected_revision": ses.get("revision", 0)},
        )
    _, created = c.request("POST", "/api/v1/sessions", {"profile": "pure_software"})
    ses = created["session"]
    _, bt = c.request(
        "POST",
        f"/api/v1/sessions/{ses['session_id']}/bench-tx",
        {"enabled": True, "expected_revision": ses["revision"]},
    )
    return bt["session"]


def frange(lo: float, hi: float, step: float) -> list[float]:
    out: list[float] = []
    x = lo
    # avoid float drift
    n = int(round((hi - lo) / step))
    for i in range(n + 1):
        out.append(round(lo + i * step, 6))
    if out[-1] != hi:
        out.append(hi)
    return out


# ── Case generators ─────────────────────────────────────────────────


def gen_kinematics_intent(dense: bool) -> Iterator[dict[str, Any]]:
    """Throttle × steer × gear × hard_brake × estop → ~1.9k–7k cases."""
    if dense:
        throttles = frange(-1.0, 1.0, 0.2)  # 11
        steers = frange(-1.0, 1.0, 0.2)  # 11
    else:
        throttles = [-1.0, -0.5, -0.1, 0.0, 0.05, 0.3, 0.6, 1.0]
        steers = [-1.0, -0.5, 0.0, 0.05, 0.5, 1.0]
    gears = [0, 1, 2, 3]
    for thr, ste, gear, hb, est in itertools.product(
        throttles, steers, gears, (False, True), (False, True)
    ):
        # Skip absurd: estop + hard_brake both true still valid — keep all
        yield {
            "suite": "kinematics_intent",
            "throttle": thr,
            "steer": ste,
            "gear": gear,
            "hard_brake": hb,
            "estop": est,
        }


def gen_analysis_host_drive(dense: bool) -> Iterator[dict[str, Any]]:
    if dense:
        speeds = list(range(-500, 3001, 250))  # 15
        yaws = list(range(-3000, 3001, 500))  # 13
    else:
        speeds = [-500, -100, 0, 100, 500, 1000, 2000, 3000]
        yaws = [-3000, -1000, 0, 1000, 3000]
    gears = [0, 1, 2, 3]
    for sp, yw, g in itertools.product(speeds, yaws, gears):
        yield {
            "suite": "analysis_host_drive",
            "speed_mmps": sp,
            "yaw_rate_mrad_s": yw,
            "gear": g,
        }


def gen_direct_motor(dense: bool) -> Iterator[dict[str, Any]]:
    if dense:
        speeds = list(range(-500, 3001, 250))
    else:
        speeds = [-500, -200, 0, 200, 450, 1000, 2000, 3000]
    gears = [0, 1, 2, 3]
    for sp, g in itertools.product(speeds, gears):
        yield {"suite": "direct_motor", "motor_speed_mmps": sp, "gear": g}


def gen_direct_steering(dense: bool) -> Iterator[dict[str, Any]]:
    if dense:
        angles = list(range(-100, 101, 10))
    else:
        angles = [-100, -50, -10, 0, 10, 40, 50, 100]
    for ang, ce, ae in itertools.product(angles, (True, False), (True, False)):
        yield {
            "suite": "direct_steering",
            "target_angle_raw": ang,
            "control_enable": ce,
            "alignment_enable": ae,
        }


def gen_direct_brake(dense: bool) -> Iterator[dict[str, Any]]:
    if dense:
        pressures = list(range(0, 101, 5))  # 0..100
    else:
        pressures = [0, 5, 10, 20, 35, 40, 50, 75, 100]
    for p, ce in itertools.product(pressures, (True, False)):
        yield {
            "suite": "direct_brake",
            "pressure_request_raw": p,
            "control_enable": ce,
        }


def gen_motion_then_brake() -> Iterator[dict[str, Any]]:
    """Multi-step: move then brake / estop / gear N (brake-while-moving)."""
    moves = [
        {"throttle": 0.5, "steer": 0.0, "gear": 1},
        {"throttle": 0.8, "steer": 0.3, "gear": 1},
        {"throttle": 1.0, "steer": -0.2, "gear": 2},
        {"throttle": -0.5, "steer": 0.0, "gear": 3},
        {"throttle": 0.4, "steer": 0.5, "gear": 1},
        {"throttle": 0.6, "steer": -0.5, "gear": 2},
    ]
    actions = [
        {"action": "hard_brake"},
        {"action": "estop"},
        {"action": "gear_n"},
        {"action": "throttle_zero"},
        {"action": "low_brake", "pressure": 40},
        {"action": "low_brake", "pressure": 80},
        {"action": "hard_brake_and_steer", "steer": 0.0},
    ]
    for m, a in itertools.product(moves, actions):
        yield {"suite": "motion_then_brake", "move": m, "then": a}


def gen_invalid() -> Iterator[dict[str, Any]]:
    """Cases that must fail validation or business rules."""
    yield {"suite": "invalid", "kind": "intent_no_bench", "body": {"sequence": 1, "throttle": 0.5, "steer": 0}}
    yield {
        "suite": "invalid",
        "kind": "intent_throttle_oob",
        "body": {"sequence": 1, "throttle": 1.5, "steer": 0},
    }
    yield {
        "suite": "invalid",
        "kind": "intent_steer_oob",
        "body": {"sequence": 1, "throttle": 0, "steer": -2},
    }
    yield {
        "suite": "invalid",
        "kind": "analysis_speed_oob",
        "body": {"speed_mmps": 9000, "yaw_rate_mrad_s": 0, "gear": 1},
    }
    yield {
        "suite": "invalid",
        "kind": "direct_unknown_channel",
        "body": {"channel": "horn", "enabled": True, "values": {}},
    }


# ── Executors ───────────────────────────────────────────────────────


def run_kinematics(c: Client, case: dict[str, Any], seq: int) -> CaseResult:
    t0 = time.perf_counter()
    thr = case["throttle"]
    ste = case["steer"]
    gear = case["gear"]
    hb = case["hard_brake"]
    est = case["estop"]
    cid = (
        f"kin thr={thr} ste={ste} g={gear} hb={int(hb)} est={int(est)}"
    )
    try:
        if est:
            code, body = c.request(
                "POST",
                "/api/v1/control/intent",
                {
                    "sequence": seq,
                    "throttle": thr,
                    "steer": ste,
                    "gear": gear,
                    "hard_brake": hb,
                    "estop": True,
                    "mode": "kinematics",
                    "source": "matrix",
                },
            )
            ok = code == 200
            # after estop control released
            ctrl = body.get("control") or {}
            ok = ok and (ctrl.get("active") is False or "estop" in body)
            # re-arm session for next cases if estop cleared session vehicle view
            return CaseResult(
                suite="kinematics_intent",
                case_id=cid,
                ok=ok,
                detail=f"estop http={code} active={ctrl.get('active')}",
                inputs=case,
                expected={"http": 200, "released_or_estop": True},
                observed={"http": code, "control": {k: ctrl.get(k) for k in ("active", "estop", "shaped_speed_mmps")}},
                ms=(time.perf_counter() - t0) * 1000,
            )

        exp_sp, exp_yw, exp_g = expected_shape(thr, ste, gear, hb)
        code, body = c.request(
            "POST",
            "/api/v1/control/intent",
            {
                "sequence": seq,
                "throttle": thr,
                "steer": ste,
                "gear": gear,
                "hard_brake": hb,
                "estop": False,
                "mode": "kinematics",
                "source": "matrix",
            },
        )
        ctrl = body.get("control") or {}
        got_sp = as_int(ctrl.get("shaped_speed_mmps"), 0)
        got_yw = as_int(ctrl.get("shaped_yaw_mrad_s"), 0)
        got_g = as_int(ctrl.get("gear"), -1)
        ok = (
            code == 200
            and ctrl.get("active") is True
            and got_sp == exp_sp
            and got_yw == exp_yw
            and got_g == exp_g
        )
        return CaseResult(
            suite="kinematics_intent",
            case_id=cid,
            ok=ok,
            detail=f"http={code} shaped=({got_sp},{got_yw},g{got_g}) exp=({exp_sp},{exp_yw},g{exp_g})",
            inputs=case,
            expected={"speed": exp_sp, "yaw": exp_yw, "gear": exp_g},
            observed={"speed": got_sp, "yaw": got_yw, "gear": got_g, "http": code},
            ms=(time.perf_counter() - t0) * 1000,
        )
    except Exception as e:
        return CaseResult(
            suite="kinematics_intent",
            case_id=cid,
            ok=False,
            detail="exception",
            inputs=case,
            error=str(e),
            ms=(time.perf_counter() - t0) * 1000,
        )


def run_analysis(c: Client, case: dict[str, Any]) -> CaseResult:
    t0 = time.perf_counter()
    cid = f"an sp={case['speed_mmps']} yw={case['yaw_rate_mrad_s']} g={case['gear']}"
    try:
        code, body = c.request(
            "POST",
            "/api/v1/analysis/host-drive",
            {
                "speed_mmps": case["speed_mmps"],
                "yaw_rate_mrad_s": case["yaw_rate_mrad_s"],
                "gear": case["gear"],
            },
        )
        ok = code == 200 and body.get("mode") == "oneshot" and body.get("ok") is True
        vals = body.get("values") or {}
        if ok:
            ok = (
                int(vals.get("speed_mmps", -99999)) == case["speed_mmps"]
                and int(vals.get("yaw_rate_mrad_s", -99999)) == case["yaw_rate_mrad_s"]
            )
        return CaseResult(
            suite="analysis_host_drive",
            case_id=cid,
            ok=ok,
            detail=f"http={code} mode={body.get('mode')}",
            inputs=case,
            expected={"http": 200, "values": case},
            observed={"http": code, "values": vals},
            ms=(time.perf_counter() - t0) * 1000,
        )
    except Exception as e:
        return CaseResult(
            suite="analysis_host_drive",
            case_id=cid,
            ok=False,
            detail="exception",
            inputs=case,
            error=str(e),
            ms=(time.perf_counter() - t0) * 1000,
        )


def run_direct_motor(c: Client, case: dict[str, Any]) -> CaseResult:
    t0 = time.perf_counter()
    cid = f"mot sp={case['motor_speed_mmps']} g={case['gear']}"
    try:
        code, body = c.request(
            "POST",
            "/api/v1/control/direct",
            {
                "channel": "motor",
                "enabled": True,
                "values": {
                    "motor_speed_mmps": case["motor_speed_mmps"],
                    "gear": case["gear"],
                },
            },
        )
        ctrl = body.get("control") or {}
        ok = code == 200 and "motor" in (ctrl.get("direct_channels") or [])
        return CaseResult(
            suite="direct_motor",
            case_id=cid,
            ok=ok,
            detail=f"http={code} ch={ctrl.get('direct_channels')}",
            inputs=case,
            observed={"http": code, "channels": ctrl.get("direct_channels")},
            ms=(time.perf_counter() - t0) * 1000,
        )
    except Exception as e:
        return CaseResult(
            suite="direct_motor",
            case_id=cid,
            ok=False,
            detail="exception",
            inputs=case,
            error=str(e),
            ms=(time.perf_counter() - t0) * 1000,
        )


def run_direct_steering(c: Client, case: dict[str, Any]) -> CaseResult:
    t0 = time.perf_counter()
    cid = f"str ang={case['target_angle_raw']} ce={case['control_enable']}"
    try:
        code, body = c.request(
            "POST",
            "/api/v1/control/direct",
            {
                "channel": "steering",
                "enabled": True,
                "values": {
                    "target_angle_raw": case["target_angle_raw"],
                    "control_enable": case["control_enable"],
                    "alignment_enable": case["alignment_enable"],
                },
            },
        )
        ctrl = body.get("control") or {}
        ok = code == 200 and "steering" in (ctrl.get("direct_channels") or [])
        return CaseResult(
            suite="direct_steering",
            case_id=cid,
            ok=ok,
            detail=f"http={code} ch={ctrl.get('direct_channels')}",
            inputs=case,
            observed={"http": code, "channels": ctrl.get("direct_channels")},
            ms=(time.perf_counter() - t0) * 1000,
        )
    except Exception as e:
        return CaseResult(
            suite="direct_steering",
            case_id=cid,
            ok=False,
            detail="exception",
            inputs=case,
            error=str(e),
            ms=(time.perf_counter() - t0) * 1000,
        )


def run_direct_brake(c: Client, case: dict[str, Any]) -> CaseResult:
    t0 = time.perf_counter()
    cid = f"brk p={case['pressure_request_raw']} ce={case['control_enable']}"
    try:
        code, body = c.request(
            "POST",
            "/api/v1/control/direct",
            {
                "channel": "brake",
                "enabled": True,
                "values": {
                    "pressure_request_raw": case["pressure_request_raw"],
                    "control_enable": case["control_enable"],
                },
            },
        )
        ctrl = body.get("control") or {}
        ok = code == 200 and "brake" in (ctrl.get("direct_channels") or [])
        return CaseResult(
            suite="direct_brake",
            case_id=cid,
            ok=ok,
            detail=f"http={code} ch={ctrl.get('direct_channels')}",
            inputs=case,
            observed={"http": code, "channels": ctrl.get("direct_channels")},
            ms=(time.perf_counter() - t0) * 1000,
        )
    except Exception as e:
        return CaseResult(
            suite="direct_brake",
            case_id=cid,
            ok=False,
            detail="exception",
            inputs=case,
            error=str(e),
            ms=(time.perf_counter() - t0) * 1000,
        )


def run_motion_then_brake(c: Client, case: dict[str, Any], seq: int) -> CaseResult:
    t0 = time.perf_counter()
    m = case["move"]
    then = case["then"]
    cid = f"mtb move={m} then={then}"
    try:
        # Phase 1: moving
        code1, b1 = c.request(
            "POST",
            "/api/v1/control/intent",
            {
                "sequence": seq,
                "mode": "kinematics",
                "source": "matrix",
                "throttle": m["throttle"],
                "steer": m["steer"],
                "gear": m["gear"],
                "hard_brake": False,
                "estop": False,
            },
        )
        ctrl1 = b1.get("control") or {}
        moving_speed = as_int(ctrl1.get("shaped_speed_mmps"), 0)
        if code1 != 200 or not ctrl1.get("active"):
            return CaseResult(
                suite="motion_then_brake",
                case_id=cid,
                ok=False,
                detail=f"move failed http={code1}",
                inputs=case,
                observed=ctrl1,
                ms=(time.perf_counter() - t0) * 1000,
            )
        if abs(m["throttle"]) >= DEADBAND and moving_speed == 0 and m["throttle"] > 0:
            # should be moving forward
            return CaseResult(
                suite="motion_then_brake",
                case_id=cid,
                ok=False,
                detail=f"expected motion but shaped=0 thr={m['throttle']}",
                inputs=case,
                observed={"shaped": moving_speed},
                ms=(time.perf_counter() - t0) * 1000,
            )

        action = then["action"]
        seq2 = seq + 1
        if action == "hard_brake":
            code2, b2 = c.request(
                "POST",
                "/api/v1/control/intent",
                {
                    "sequence": seq2,
                    "mode": "kinematics",
                    "throttle": m["throttle"],
                    "steer": m["steer"],
                    "gear": m["gear"],
                    "hard_brake": True,
                    "estop": False,
                },
            )
            ctrl2 = b2.get("control") or {}
            exp_sp, exp_yw, exp_g = expected_shape(
                m["throttle"], m["steer"], m["gear"], True
            )
            ok = (
                code2 == 200
                and as_int(ctrl2.get("shaped_speed_mmps")) == exp_sp
                and as_int(ctrl2.get("gear")) == exp_g
                and exp_sp == 0
                and exp_g == GEAR_N
            )
            return CaseResult(
                suite="motion_then_brake",
                case_id=cid,
                ok=ok,
                detail=f"hard_brake after move={moving_speed} → shaped={ctrl2.get('shaped_speed_mmps')} gear={ctrl2.get('gear')}",
                inputs=case,
                expected={"speed": 0, "gear": GEAR_N},
                observed={
                    "move_speed": moving_speed,
                    "after_speed": ctrl2.get("shaped_speed_mmps"),
                    "after_gear": ctrl2.get("gear"),
                },
                ms=(time.perf_counter() - t0) * 1000,
            )

        if action == "estop":
            code2, b2 = c.request(
                "POST",
                "/api/v1/control/intent",
                {
                    "sequence": seq2,
                    "throttle": 0,
                    "steer": 0,
                    "estop": True,
                },
            )
            ok = code2 == 200
            # re-enable bench after estop for matrix continuity
            ensure_bench(c)
            return CaseResult(
                suite="motion_then_brake",
                case_id=cid,
                ok=ok,
                detail=f"estop after move={moving_speed} http={code2}",
                inputs=case,
                observed={"http": code2, "move_speed": moving_speed},
                ms=(time.perf_counter() - t0) * 1000,
            )

        if action == "gear_n":
            code2, b2 = c.request(
                "POST",
                "/api/v1/control/intent",
                {
                    "sequence": seq2,
                    "mode": "kinematics",
                    "throttle": 0,
                    "steer": 0,
                    "gear": GEAR_N,
                    "hard_brake": False,
                },
            )
            ctrl2 = b2.get("control") or {}
            ok = code2 == 200 and as_int(ctrl2.get("shaped_speed_mmps")) == 0
            return CaseResult(
                suite="motion_then_brake",
                case_id=cid,
                ok=ok,
                detail=f"gear N coast shaped={ctrl2.get('shaped_speed_mmps')}",
                inputs=case,
                observed=ctrl2,
                ms=(time.perf_counter() - t0) * 1000,
            )

        if action == "throttle_zero":
            code2, b2 = c.request(
                "POST",
                "/api/v1/control/intent",
                {
                    "sequence": seq2,
                    "mode": "kinematics",
                    "throttle": 0,
                    "steer": m["steer"],
                    "gear": m["gear"],
                },
            )
            ctrl2 = b2.get("control") or {}
            ok = code2 == 200 and as_int(ctrl2.get("shaped_speed_mmps")) == 0
            return CaseResult(
                suite="motion_then_brake",
                case_id=cid,
                ok=ok,
                detail=f"throttle0 shaped={ctrl2.get('shaped_speed_mmps')}",
                inputs=case,
                observed=ctrl2,
                ms=(time.perf_counter() - t0) * 1000,
            )

        if action == "low_brake":
            # Keep high motion job, add low brake (direct preempts kinematics!)
            # Documented: direct preempts high — so after low brake, kinematics job gone
            code2, b2 = c.request(
                "POST",
                "/api/v1/control/direct",
                {
                    "channel": "brake",
                    "enabled": True,
                    "values": {
                        "pressure_request_raw": then.get("pressure", 40),
                        "control_enable": True,
                    },
                },
            )
            ctrl2 = b2.get("control") or {}
            ok = code2 == 200 and "brake" in (ctrl2.get("direct_channels") or [])
            # restore kinematics path for next cases
            c.request("POST", "/api/v1/control/release", {"reason": "matrix_restore"})
            return CaseResult(
                suite="motion_then_brake",
                case_id=cid,
                ok=ok,
                detail=f"low_brake p={then.get('pressure')} ch={ctrl2.get('direct_channels')} (direct preempts high)",
                inputs=case,
                observed={"channels": ctrl2.get("direct_channels"), "mode": ctrl2.get("mode")},
                ms=(time.perf_counter() - t0) * 1000,
            )

        if action == "hard_brake_and_steer":
            code2, b2 = c.request(
                "POST",
                "/api/v1/control/intent",
                {
                    "sequence": seq2,
                    "mode": "kinematics",
                    "throttle": m["throttle"],
                    "steer": then.get("steer", 0),
                    "gear": m["gear"],
                    "hard_brake": True,
                },
            )
            ctrl2 = b2.get("control") or {}
            ok = (
                code2 == 200
                and as_int(ctrl2.get("shaped_speed_mmps")) == 0
                and as_int(ctrl2.get("gear")) == GEAR_N
            )
            return CaseResult(
                suite="motion_then_brake",
                case_id=cid,
                ok=ok,
                detail=f"hard_brake+steer0 speed={ctrl2.get('shaped_speed_mmps')}",
                inputs=case,
                observed=ctrl2,
                ms=(time.perf_counter() - t0) * 1000,
            )

        return CaseResult(
            suite="motion_then_brake",
            case_id=cid,
            ok=False,
            detail=f"unknown action {action}",
            inputs=case,
            ms=(time.perf_counter() - t0) * 1000,
        )
    except Exception as e:
        return CaseResult(
            suite="motion_then_brake",
            case_id=cid,
            ok=False,
            detail="exception",
            inputs=case,
            error=str(e) + "\n" + traceback.format_exc(limit=2),
            ms=(time.perf_counter() - t0) * 1000,
        )


def run_invalid(c: Client, case: dict[str, Any]) -> CaseResult:
    t0 = time.perf_counter()
    kind = case["kind"]
    try:
        if kind == "intent_no_bench":
            # disable bench
            code, st = c.request("GET", "/api/v1/status")
            ses = (st or {}).get("session") or {}
            if ses.get("session_id"):
                c.request(
                    "POST",
                    f"/api/v1/sessions/{ses['session_id']}/bench-tx",
                    {"enabled": False, "expected_revision": ses.get("revision", 0)},
                )
            code, body = c.request("POST", "/api/v1/control/intent", case["body"])
            ok = code == 409
            ensure_bench(c)
            return CaseResult(
                suite="invalid",
                case_id=kind,
                ok=ok,
                detail=f"http={code}",
                inputs=case,
                expected={"http": 409},
                observed={"http": code, "code": body.get("code") if isinstance(body, dict) else None},
                ms=(time.perf_counter() - t0) * 1000,
            )

        if kind == "intent_throttle_oob":
            code, _ = c.request("POST", "/api/v1/control/intent", case["body"])
            return CaseResult(
                suite="invalid",
                case_id=kind,
                ok=code == 422,
                detail=f"http={code}",
                expected={"http": 422},
                observed={"http": code},
                ms=(time.perf_counter() - t0) * 1000,
            )

        if kind == "intent_steer_oob":
            code, _ = c.request("POST", "/api/v1/control/intent", case["body"])
            return CaseResult(
                suite="invalid",
                case_id=kind,
                ok=code == 422,
                detail=f"http={code}",
                expected={"http": 422},
                observed={"http": code},
                ms=(time.perf_counter() - t0) * 1000,
            )

        if kind == "analysis_speed_oob":
            code, _ = c.request("POST", "/api/v1/analysis/host-drive", case["body"])
            return CaseResult(
                suite="invalid",
                case_id=kind,
                ok=code == 422,
                detail=f"http={code}",
                expected={"http": 422},
                observed={"http": code},
                ms=(time.perf_counter() - t0) * 1000,
            )

        if kind == "direct_unknown_channel":
            code, body = c.request("POST", "/api/v1/control/direct", case["body"])
            return CaseResult(
                suite="invalid",
                case_id=kind,
                ok=code in (400, 409, 422),
                detail=f"http={code}",
                expected={"http_in": [400, 409, 422]},
                observed={"http": code, "body": body},
                ms=(time.perf_counter() - t0) * 1000,
            )

        return CaseResult(
            suite="invalid",
            case_id=kind,
            ok=False,
            detail="unknown invalid kind",
            inputs=case,
            ms=(time.perf_counter() - t0) * 1000,
        )
    except Exception as e:
        return CaseResult(
            suite="invalid",
            case_id=kind,
            ok=False,
            detail="exception",
            error=str(e),
            ms=(time.perf_counter() - t0) * 1000,
        )


def count_cases(dense: bool) -> dict[str, int]:
    return {
        "kinematics_intent": sum(1 for _ in gen_kinematics_intent(dense)),
        "analysis_host_drive": sum(1 for _ in gen_analysis_host_drive(dense)),
        "direct_motor": sum(1 for _ in gen_direct_motor(dense)),
        "direct_steering": sum(1 for _ in gen_direct_steering(dense)),
        "direct_brake": sum(1 for _ in gen_direct_brake(dense)),
        "motion_then_brake": sum(1 for _ in gen_motion_then_brake()),
        "invalid": sum(1 for _ in gen_invalid()),
    }


def run_matrix(
    *,
    dense: bool,
    live_base: str | None,
    limit: int | None,
    suites: set[str] | None,
) -> dict[str, Any]:
    mode = "live" if live_base else "inprocess"
    c = Client(mode, live_base)
    results: list[CaseResult] = []
    started = datetime.now(timezone.utc).isoformat()
    t_all = time.perf_counter()

    try:
        ensure_bench(c)
        seq = 1

        def take(it: Iterator[dict[str, Any]]) -> list[dict[str, Any]]:
            items = list(it)
            if limit is not None:
                return items[:limit]
            return items

        plan: list[tuple[str, list[dict[str, Any]], Callable[..., CaseResult]]] = []

        def want(name: str) -> bool:
            return suites is None or name in suites

        if want("kinematics_intent"):
            plan.append(
                (
                    "kinematics_intent",
                    take(gen_kinematics_intent(dense)),
                    lambda case, s=0: run_kinematics(c, case, s),
                )
            )
        if want("analysis_host_drive"):
            plan.append(
                (
                    "analysis_host_drive",
                    take(gen_analysis_host_drive(dense)),
                    lambda case, s=0: run_analysis(c, case),
                )
            )
        if want("direct_motor"):
            plan.append(
                (
                    "direct_motor",
                    take(gen_direct_motor(dense)),
                    lambda case, s=0: run_direct_motor(c, case),
                )
            )
        if want("direct_steering"):
            plan.append(
                (
                    "direct_steering",
                    take(gen_direct_steering(dense)),
                    lambda case, s=0: run_direct_steering(c, case),
                )
            )
        if want("direct_brake"):
            plan.append(
                (
                    "direct_brake",
                    take(gen_direct_brake(dense)),
                    lambda case, s=0: run_direct_brake(c, case),
                )
            )
        if want("motion_then_brake"):
            plan.append(
                (
                    "motion_then_brake",
                    take(gen_motion_then_brake()),
                    lambda case, s=0: run_motion_then_brake(c, case, s),
                )
            )
        if want("invalid"):
            plan.append(
                (
                    "invalid",
                    take(gen_invalid()),
                    lambda case, s=0: run_invalid(c, case),
                )
            )

        total = sum(len(cases) for _, cases, _ in plan)
        print(f"Matrix mode={mode} dense={dense} total_cases={total}")
        done = 0
        for suite_name, cases, _runner in plan:
            print(f"  suite {suite_name}: {len(cases)} cases")
            try:
                ensure_bench(c)
                seq = 1
            except Exception as e:
                print(f"    WARN ensure_bench: {e}")

            for case in cases:
                if suite_name == "kinematics_intent":
                    r = run_kinematics(c, case, seq)
                    seq += 1
                    if case.get("estop"):
                        try:
                            ensure_bench(c)
                            seq = 1
                        except Exception:
                            pass
                elif suite_name == "motion_then_brake":
                    r = run_motion_then_brake(c, case, seq)
                    seq += 3
                    if (case.get("then") or {}).get("action") == "estop":
                        try:
                            ensure_bench(c)
                            seq = 1
                        except Exception:
                            pass
                elif suite_name == "analysis_host_drive":
                    r = run_analysis(c, case)
                elif suite_name == "direct_motor":
                    r = run_direct_motor(c, case)
                elif suite_name == "direct_steering":
                    r = run_direct_steering(c, case)
                elif suite_name == "direct_brake":
                    r = run_direct_brake(c, case)
                elif suite_name == "invalid":
                    r = run_invalid(c, case)
                else:
                    r = CaseResult(
                        suite=suite_name,
                        case_id="unknown",
                        ok=False,
                        detail="no runner",
                        inputs=case,
                    )
                results.append(r)
                done += 1
                if done % 250 == 0:
                    fails = sum(1 for x in results if not x.ok)
                    print(f"    … {done}/{total} (fails so far {fails})")

            c.request("POST", "/api/v1/control/release", {"reason": "suite_boundary"})
            c.request("POST", "/api/v1/analysis/stop", {})

    finally:
        c.close()

    elapsed = time.perf_counter() - t_all
    passed = [r for r in results if r.ok]
    failed = [r for r in results if not r.ok]
    by_suite: dict[str, dict[str, int]] = {}
    for r in results:
        by_suite.setdefault(r.suite, {"pass": 0, "fail": 0})
        by_suite[r.suite]["pass" if r.ok else "fail"] += 1

    report = {
        "started": started,
        "mode": mode,
        "dense": dense,
        "elapsed_s": round(elapsed, 3),
        "summary": {
            "total": len(results),
            "passed": len(passed),
            "failed": len(failed),
            "pass_rate": round(len(passed) / len(results), 6) if results else 0,
            "by_suite": by_suite,
            "planned_counts": count_cases(dense),
        },
        "failures": [asdict(r) for r in failed[:500]],  # cap file size
        "failure_count_full": len(failed),
    }
    return report


def write_report(report: dict[str, Any], out_dir: Path) -> tuple[Path, Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    jp = out_dir / f"matrix_qa_{stamp}.json"
    mp = out_dir / f"matrix_qa_{stamp}.md"
    latest_j = out_dir / "matrix_qa_latest.json"
    latest_m = out_dir / "matrix_qa_latest.md"
    text = json.dumps(report, indent=2, default=str)
    jp.write_text(text, encoding="utf-8")
    latest_j.write_text(text, encoding="utf-8")

    s = report["summary"]
    lines = [
        f"# Combination matrix QA — {report['started']}",
        "",
        f"- mode: `{report['mode']}` dense=`{report['dense']}`",
        f"- elapsed: **{report['elapsed_s']} s**",
        f"- result: **{s['passed']}/{s['total']}** passed ({100 * s['pass_rate']:.2f}%)",
        "",
        "## Planned counts",
        "",
        "```json",
        json.dumps(s.get("planned_counts"), indent=2),
        "```",
        "",
        "## By suite",
        "",
        "| Suite | Pass | Fail |",
        "|-------|------|------|",
    ]
    for name, st in sorted((s.get("by_suite") or {}).items()):
        lines.append(f"| `{name}` | {st['pass']} | {st['fail']} |")
    fails = report.get("failures") or []
    if fails:
        lines += ["", f"## Failures (first {len(fails)})", ""]
        for f in fails[:80]:
            lines.append(f"### `{f.get('case_id')}` ({f.get('suite')})")
            lines.append(f"- {f.get('detail')}")
            if f.get("error"):
                lines.append(f"- error: `{str(f.get('error'))[:300]}`")
            lines.append("")
    md = "\n".join(lines) + "\n"
    mp.write_text(md, encoding="utf-8")
    latest_m.write_text(md, encoding="utf-8")
    return jp, mp


def main() -> int:
    p = argparse.ArgumentParser(description="Thousands of control combination tests")
    p.add_argument(
        "--dense",
        action="store_true",
        help="Full dense grids (thousands of cases). Default is still multi-thousand with kinematics dense steps.",
    )
    p.add_argument(
        "--quick",
        action="store_true",
        help="Smaller grids for a fast smoke (~hundreds).",
    )
    p.add_argument("--base", default=None, help="Live API base URL (else in-process TestClient)")
    p.add_argument("--limit", type=int, default=None, help="Max cases per suite (debug)")
    p.add_argument(
        "--suite",
        action="append",
        default=None,
        help="Run only named suite(s); can repeat",
    )
    p.add_argument("--out", default=str(OUT_DIR))
    args = p.parse_args()

    dense = bool(args.dense)
    if args.quick:
        dense = False
    else:
        # default: dense kinematics for "thousands"
        dense = True if not args.quick else False
        # User asked for thousands — default dense=True
        dense = not args.quick

    suites = set(args.suite) if args.suite else None
    counts = count_cases(dense)
    total_plan = sum(counts.values())
    print("Planned case counts:", json.dumps(counts, indent=2))
    print("Total planned:", total_plan)

    report = run_matrix(
        dense=dense,
        live_base=args.base,
        limit=args.limit,
        suites=suites,
    )
    jp, mp = write_report(report, Path(args.out))
    s = report["summary"]
    print("\n=== MATRIX SUMMARY ===")
    print(f"passed {s['passed']} / {s['total']} ({100 * s['pass_rate']:.2f}%)")
    print(f"elapsed {report['elapsed_s']}s")
    print(f"report {jp}")
    print(f"md     {mp}")
    if s["failed"]:
        print(f"FAILURES: {s['failed']} (see report; first few:)")
        for f in (report.get("failures") or [])[:15]:
            print(f"  - [{f['suite']}] {f['case_id']}: {f['detail']}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
