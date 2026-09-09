"""Safety predicates over the decoded low-bus stream.

All predicates are pure functions of what the *real* nodes emitted, read through
the capture timeline. They encode the distributed ESTOP / authority contract so
scenario assertions read as intent, not as frame archaeology.

Every read is freshness-bounded: a value that stops arriving (stream loss) is
never mistaken for a live "safe" value — a predicate returns its *unsafe*
default when the stream is stale, which is exactly what a temporal MUST-NEVER
assertion must see.

Reference facts (can-dictionary.md / architecture.md):
  * SYS owns the safety latch. 0x011 estop_active + 0x7FE heartbeat + mode.
  * RT owns drive/steer authority and must zero 0x204 while SYS is in ESTOP.
  * MTR owns the physical relay/DAC and rearm; 0x206 echoes the command,
    0x502 exposes node_state/output_enabled/estop_latched.
"""
from __future__ import annotations

import time
from typing import Optional

from bench.capture import Capture, Sample
from bench.wire import (
    LOW,
    MTR_MOTOR_FBK,
    MTR_NODE_STATUS,
    RT_DRIVE_CMD,
    RT_STATE_RPT,
    SYS_HEARTBEAT,
    SYS_MODE_CMD,
    SYS_PWR_CMD,
    SYS_SAFETY_STS,
)

AUTO = 1
MANUAL = 0
ESTOP = 2

# Stream-loss timeout per message family (safety-critical margins: a stale
# value must read as unsafe rather than as a live good value).
_SYS_STS_AGE = 0.7      # 0x011 @ 200 ms
_SYS_HB_AGE = 0.5       # 0x7FE @ 100 ms
_RT_CMD_AGE = 0.3       # 0x204 @ 10 ms
_MTR_FBK_AGE = 0.3      # 0x206 / 0x502 @ 20 ms
_MODE_AGE = 0.6         # 0x110 @ 100 ms
_PWR_AGE = 0.6          # 0x113 @ 100 ms
_RT_STATE_AGE = 0.6     # 0x210 @ 100 ms


def _latest(capture: Capture, key: str) -> Optional[Sample]:
    return capture.latest_on(LOW, key)


def _fresh(sample: Optional[Sample], age: float) -> bool:
    if sample is None or not sample.is_valid:
        return False
    return (time.perf_counter() - sample.ts) <= age


def _field(sample: Optional[Sample], name: str, default):
    if sample is None or not sample.is_valid:
        return default
    return sample.values.get(name, default)


# ── SYS state ─────────────────────────────────────────────────────────
def sys_estop_active(capture: Capture, age: float = _SYS_STS_AGE) -> bool:
    sample = _latest(capture, SYS_SAFETY_STS)
    return _field(sample, "estop_active", 1) if _fresh(sample, age) else True


def sys_heartbeat_ok(capture: Capture, age: float = _SYS_HB_AGE) -> bool:
    sample = _latest(capture, SYS_HEARTBEAT)
    return _field(sample, "heartbeat_ok", 0) if _fresh(sample, age) else False


def sys_mode(capture: Capture, age: float = _MODE_AGE) -> int:
    sample = _latest(capture, SYS_MODE_CMD)
    return _field(sample, "mode", MANUAL) if _fresh(sample, age) else MANUAL


def sys_power(capture: Capture, age: float = _PWR_AGE) -> int:
    sample = _latest(capture, SYS_PWR_CMD)
    return _field(sample, "power_state", 0) if _fresh(sample, age) else 0


# ── RT state ───────────────────────────────────────────────────────────
def rt_drive_cmd(capture: Capture, age: float = _RT_CMD_AGE) -> int:
    """0x204 motor_speed_mmps (0 == no traction commanded)."""
    sample = _latest(capture, RT_DRIVE_CMD)
    return _field(sample, "motor_speed_mmps", 0) if _fresh(sample, age) else 0


def rt_mode(capture: Capture, age: float = _RT_STATE_AGE) -> int:
    sample = _latest(capture, RT_STATE_RPT)
    return _field(sample, "mode", 0) if _fresh(sample, age) else 0


# ── MTR state ──────────────────────────────────────────────────────────
def mtr_output_enabled(capture: Capture, age: float = _MTR_FBK_AGE) -> bool:
    sample = _latest(capture, MTR_NODE_STATUS)
    return _field(sample, "output_enabled", 0) if _fresh(sample, age) else False


def mtr_estop_latched(capture: Capture, age: float = _MTR_FBK_AGE) -> bool:
    sample = _latest(capture, MTR_NODE_STATUS)
    return _field(sample, "estop_latched", 1) if _fresh(sample, age) else True


def mtr_estop_active(capture: Capture, age: float = _MTR_FBK_AGE) -> bool:
    sample = _latest(capture, MTR_NODE_STATUS)
    return _field(sample, "estop_active", 1) if _fresh(sample, age) else True


def mtr_ready(capture: Capture, age: float = _MTR_FBK_AGE) -> bool:
    sample = _latest(capture, MTR_NODE_STATUS)
    return _field(sample, "ready", 0) if _fresh(sample, age) else False


def mtr_reported_speed(capture: Capture, age: float = _MTR_FBK_AGE) -> int:
    """0x206 echoes the commanded speed, never a true measured speed."""
    sample = _latest(capture, MTR_MOTOR_FBK)
    return _field(sample, "motor_command_speed_mmps", 0) if _fresh(sample, age) else 0


# ── composite danger / readiness predicates ────────────────────────────
def traction_commanded(capture: Capture) -> bool:
    return rt_drive_cmd(capture) != 0


def motor_unsafe(capture: Capture) -> bool:
    """MUST NEVER be true: physical output authority while SYS is latched ESTOP."""
    return sys_estop_active(capture) and mtr_output_enabled(capture)


def motion_unsafe(capture: Capture) -> bool:
    """MUST NEVER be true: nonzero traction command while SYS is latched ESTOP."""
    return sys_estop_active(capture) and traction_commanded(capture)


def all_nodes_ready(capture: Capture, timeout: float = 10.0) -> None:
    """Wait for the healthy RT/SYS/MTR + restbus precondition of a scenario."""
    from assertions.temporal import wait_for_valid

    wait_for_valid(capture, LOW, SYS_HEARTBEAT, timeout=timeout)
    wait_for_valid(capture, LOW, RT_DRIVE_CMD, timeout=timeout)
    wait_for_valid(capture, LOW, MTR_NODE_STATUS, timeout=timeout)
