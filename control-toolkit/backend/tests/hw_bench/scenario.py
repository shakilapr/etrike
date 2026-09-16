"""Shared helpers for the long-running H8/H9/H10 hardware-bench suites.

These build on ``harness`` but add three things the scenario suites need:

* ``dur`` — scale every phase duration with ``ETRIKE_BENCH_SCENARIO_SCALE`` so a
  full 60 s scenario can be smoke-run in a few seconds.
* ``health`` — one snapshot of every low-level unit command plus node health, so
  an invariant can be asserted while a maneuver is in progress.
* ``hold`` — sample ``health`` for a duration and fail if either node latches
  ESTOP mid-maneuver (the core real-time invariant).
"""

from __future__ import annotations

import os
import time
from typing import Any, Callable, Optional

from harness import (
    CAN_RT_BRAKE_CMD,
    CAN_RT_DRIVE_CMD,
    CAN_RT_NODE_STATUS,
    CAN_RT_STATE_RPT,
    CAN_SEB_REQ,
    CAN_SES_REQ,
    CAN_SYS_SAFETY_STS,
    LOW,
    SEB_MODE_PRESSURE,
    SEB_MODE_STROKE,
    StateMap,
    signal_of,
)

# 1.0 = real time; 0.1 = 10x faster smoke run. Override per invocation.
SCENARIO_SCALE = float(os.environ.get("ETRIKE_BENCH_SCENARIO_SCALE", "1.0"))

# steering_control.h: 0 deg -> raw 30000; dynamic clamp keeps |angle| <= 450*0.1deg
SBW_ANGLE_OFFSET = 30000


def dur(seconds: float) -> float:
    """Scale a phase duration (never below 0.4 s so sampling still runs)."""
    return max(0.4, seconds * SCENARIO_SCALE)


def health(bench) -> dict[str, Any]:
    """Snapshot the low-level unit commands and node health in one read."""
    s = bench.state_map()

    def g(cid: int, name: str):
        return signal_of(s.get((LOW, int(cid))), name)

    snapshot = {
        "map": s,
        "sys_estop": g(CAN_SYS_SAFETY_STS, "estop_active"),
        "rt_estop": g(CAN_RT_NODE_STATUS, "estop_active"),
        "rt_degraded": g(CAN_RT_NODE_STATUS, "degraded"),
        "rt_mode": g(CAN_RT_STATE_RPT, "mode"),
        "task_health": g(CAN_RT_STATE_RPT, "task_health"),
        "speed": g(CAN_RT_DRIVE_CMD, "motor_speed_mmps"),
        "gear": g(CAN_RT_DRIVE_CMD, "gear"),
        "brake": g(CAN_RT_BRAKE_CMD, "brake_pressure_kpa"),
        "steer_raw": g(CAN_SES_REQ, "target_angle_raw"),
        "seb_mode": g(CAN_SEB_REQ, "control_mode"),
        "seb_stroke": g(CAN_SEB_REQ, "stroke_request_raw"),
        "seb_pressure": g(CAN_SEB_REQ, "pressure_request_raw"),
    }
    # All Low-bus unit frames present in this sample. A transient gap (e.g. a
    # CANalyst USB reconnect that clears the backend state ring) must not be
    # mistaken for a safety trip.
    snapshot["low_live"] = all(
        snapshot[key] is not None
        for key in ("sys_estop", "rt_estop", "rt_mode", "task_health",
                    "speed", "brake", "steer_raw", "seb_mode")
    )
    return snapshot


def assert_no_trip(snapshot: dict[str, Any], label: str) -> None:
    """No node may latch ESTOP during a nominal maneuver."""
    assert snapshot["sys_estop"] == 0 and snapshot["rt_estop"] == 0, (
        f"[{label}] unexpected ESTOP: sys={snapshot['sys_estop']} rt={snapshot['rt_estop']} "
        f"rt_mode={snapshot['rt_mode']} seb_stroke={snapshot['seb_stroke']} "
        f"brake={snapshot['brake']} speed={snapshot['speed']}"
    )


def hold(bench, label: str, seconds: float, sample_s: float = 0.5,
         max_missing: int = 6) -> dict[str, Any]:
    """Sample health for ``seconds``, failing if a node trips mid-maneuver.

    Transient Low-bus telemetry gaps (adapter reconnect) are tolerated for up
    to ``max_missing`` consecutive samples; a sustained gap fails with a
    telemetry message rather than a misleading ESTOP one.
    """
    deadline = time.monotonic() + dur(seconds)
    snapshot = health(bench)
    missing = 0
    while time.monotonic() < deadline:
        snapshot = health(bench)
        if snapshot["low_live"]:
            missing = 0
            assert_no_trip(snapshot, label)
        else:
            missing += 1
            assert missing <= max_missing, (
                f"[{label}] Low-bus telemetry missing for {missing} samples "
                f"(adapter reconnect?): {snapshot}"
            )
        time.sleep(sample_s)
    return snapshot


def track(bench, predicate: Callable[[StateMap], bool], label: str, timeout_s: float = 6.0):
    """Wait for ``predicate``; on failure report all three unit frames."""
    ok, state = bench.wait_for(predicate, timeout_s=timeout_s)
    assert ok, (
        f"{label}: 0x204={state.get((LOW, CAN_RT_DRIVE_CMD))} "
        f"0x205={state.get((LOW, CAN_RT_BRAKE_CMD))} "
        f"0x169={state.get((LOW, CAN_SES_REQ))} "
        f"0x7B9={state.get((LOW, CAN_SEB_REQ))}"
    )
    return state


# ── predicates ───────────────────────────────────────────────────────────
def speed_is(mmps: int, tol: int = 120):
    return lambda s: abs(
        (signal_of(s.get((LOW, CAN_RT_DRIVE_CMD)), "motor_speed_mmps") or 0) - mmps
    ) <= tol


def gear_is(gear: int):
    return lambda s: signal_of(s.get((LOW, CAN_RT_DRIVE_CMD)), "gear") == gear


def steer_gt(raw: int):
    return lambda s: (signal_of(s.get((LOW, CAN_SES_REQ)), "target_angle_raw") or 0) > raw


def steer_lt(raw: int):
    return lambda s: (signal_of(s.get((LOW, CAN_SES_REQ)), "target_angle_raw") or 0) < raw


def brake_is(kpa: int, tol: int = 0):
    return lambda s: abs(
        (signal_of(s.get((LOW, CAN_RT_BRAKE_CMD)), "brake_pressure_kpa") or 0) - kpa
    ) <= tol


def brake_above(kpa: int):
    return lambda s: (signal_of(s.get((LOW, CAN_RT_BRAKE_CMD)), "brake_pressure_kpa") or 0) > kpa


def seb_pressure(raw: Optional[int] = None):
    def predicate(s: StateMap) -> bool:
        if signal_of(s.get((LOW, CAN_SEB_REQ)), "control_mode") != SEB_MODE_PRESSURE:
            return False
        return raw is None or signal_of(s.get((LOW, CAN_SEB_REQ)), "pressure_request_raw") == raw

    return predicate


def seb_stroke(raw: Optional[int] = None):
    def predicate(s: StateMap) -> bool:
        if signal_of(s.get((LOW, CAN_SEB_REQ)), "control_mode") != SEB_MODE_STROKE:
            return False
        return raw is None or signal_of(s.get((LOW, CAN_SEB_REQ)), "stroke_request_raw") == raw

    return predicate
