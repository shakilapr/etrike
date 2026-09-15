"""H3 — drive kinematics: Host 0x300 -> RT 0x204 and the host watchdog.

RT grants motion only when safety (0x011), mode (0x110) and host drive (0x300)
are all fresh; the resulting setpoint/gear is published on 0x204 for MTR.
"""

from __future__ import annotations

import time

import pytest

from harness import (
    CAN_RT_DRIVE_CMD,
    CAN_RT_NODE_STATUS,
    CAN_SYS_SAFETY_STS,
    GEAR_D,
    GEAR_N,
    GEAR_R,
    LOW,
    signal_of,
)

pytestmark = pytest.mark.hw_bench


def _drive_cmd_is(state, *, speed, gear, tol=100):
    value = signal_of(state.get((LOW, CAN_RT_DRIVE_CMD)), "motor_speed_mmps")
    current_gear = signal_of(state.get((LOW, CAN_RT_DRIVE_CMD)), "gear")
    return value is not None and abs(value - speed) <= tol and current_gear == gear


def test_forward_drive_1000(auto_ready):
    """Stream 0x300 @ 50 Hz, 1000 mm/s, gear D -> RT 0x204 matches."""
    bench = auto_ready
    bench.start_drive(1000, yaw_rate_mrad_s=0, gear=GEAR_D)
    ok, state = bench.wait_for(lambda s: _drive_cmd_is(s, speed=1000, gear=GEAR_D), timeout_s=4.0)
    assert ok, f"RT 0x204 never tracked 1000 mm/s / gear D: {state.get((LOW, CAN_RT_DRIVE_CMD))}"


def test_reverse_drive_minus_500(auto_ready):
    """Command -500 mm/s gear R -> RT 0x204 reports negative speed in reverse."""
    bench = auto_ready
    bench.start_drive(-500, yaw_rate_mrad_s=0, gear=GEAR_R)
    ok, state = bench.wait_for(lambda s: _drive_cmd_is(s, speed=-500, gear=GEAR_R), timeout_s=4.0)
    assert ok, f"RT 0x204 never tracked -500 mm/s / gear R: {state.get((LOW, CAN_RT_DRIVE_CMD))}"


def test_zero_setpoint_is_neutral(auto_ready):
    """Zero speed default gear resolves to N."""
    bench = auto_ready
    bench.start_drive(0, gear=GEAR_N)
    ok, state = bench.wait_for(lambda s: _drive_cmd_is(s, speed=0, gear=GEAR_N, tol=5), timeout_s=3.0)
    assert ok, f"RT 0x204 did not settle to 0 mm/s / gear N: {state.get((LOW, CAN_RT_DRIVE_CMD))}"


def test_host_stream_loss_does_not_latch_estop_in_bench_mode(auto_ready):
    """Bench solo mode bypasses the host watchdog: stream loss must not latch.

    The rig runs ``hardware_bench`` in developer bypass mode, so the production
    host-command watchdog (rt-esp32/src/main.cpp, guarded by ``g_bench_solo_mode``)
    is intentionally inert. This asserts the observable consequence: dropping the
    host 0x300 stream mid-drive neither ESTOPs the vehicle nor decays the setpoint
    to zero — the last commanded setpoint is held.
    """
    bench = auto_ready
    job = bench.start_drive(1200, gear=GEAR_D)
    assert job, "failed to start periodic host drive stream"
    ok, _ = bench.wait_for(lambda s: _drive_cmd_is(s, speed=1200, gear=GEAR_D), timeout_s=4.0)
    assert ok, "RT 0x204 never reached 1200 mm/s before stream-loss check"

    bench.cancel_injection(job)
    time.sleep(1.5)

    state = bench.state_map()
    estop = signal_of(state.get((LOW, CAN_SYS_SAFETY_STS)), "estop_active")
    assert estop == 0, "host stream loss wrongly latched SYS ESTOP in bench bypass mode"
    rt_ns = state.get((LOW, CAN_RT_NODE_STATUS)) or {}
    assert signal_of(rt_ns, "estop_active") == 0, "RT latched ESTOP on host stream loss"
    assert signal_of(rt_ns, "block_mask") == 0, "RT block_mask set on host stream loss"

    speed = signal_of(state.get((LOW, CAN_RT_DRIVE_CMD)), "motor_speed_mmps")
    assert speed and speed >= 1000, (
        f"bench solo mode should hold the last setpoint, got {speed} mm/s"
    )
