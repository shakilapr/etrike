"""H6 — dynamic multi-step maneuvers on the actuator-less bench.

All maneuvers are verified through output commands only. No peer actuator
feedback is injected; this is the developer bypass mode in action.
"""

from __future__ import annotations

import time

import pytest

from harness import (
    CAN_RT_BRAKE_CMD,
    CAN_RT_DRIVE_CMD,
    CAN_SYS_SAFETY_STS,
    GEAR_D,
    LOW,
    OBSTACLE_CLEAR,
    signal_of,
)

pytestmark = pytest.mark.hw_bench


def test_acceleration_ramp_tracks_targets(auto_ready):
    """AUTO drive tracks a 500 -> 1200 -> 2200 mm/s ramp on 0x204."""
    bench = auto_ready
    for speed in (500, 1200, 2200):
        bench.start_drive(speed, gear=GEAR_D)
        ok, state = bench.wait_for(
            lambda s, target=speed: abs(
                (signal_of(s.get((LOW, CAN_RT_DRIVE_CMD)), "motor_speed_mmps") or 0) - target
            ) <= 100,
            timeout_s=4.0,
        )
        assert ok, (
            f"RT 0x204 failed to track {speed} mm/s: "
            f"{state.get((LOW, CAN_RT_DRIVE_CMD))}"
        )


def _send_lights_until(bench, *, left: int, right: int, timeout_s: float = 5.0):
    """Retry the High->Low light command until SYS reflects it.

    The 0x302 forward is best-effort (single non-blocking gateway frame), so a
    single injection can be dropped; the turn lamps also blink at 500 ms.
    """
    deadline = time.monotonic() + timeout_s
    state = {}
    while time.monotonic() < deadline:
        bench.send_lights(left=left, right=right)
        ok, state = bench.wait_signal(
            LOW, CAN_SYS_SAFETY_STS, "light_left", expected=left, timeout_s=1.0
        )
        if not ok:
            continue
        ok, state = bench.wait_signal(
            LOW, CAN_SYS_SAFETY_STS, "light_right", expected=right, timeout_s=1.0
        )
        if ok:
            return True, state
    return False, state


def test_cornering_activates_turn_lights(auto_ready):
    """Left/right turn commands are forwarded High->Low and actuated by SYS."""
    bench = auto_ready
    bench.start_drive(1500, yaw_rate_mrad_s=400, gear=GEAR_D)

    ok, state = _send_lights_until(bench, left=1, right=0)
    assert ok, f"SYS 0x011 light_left never asserted: {state.get((LOW, CAN_SYS_SAFETY_STS))}"

    ok, state = _send_lights_until(bench, left=0, right=1)
    assert ok, f"SYS 0x011 light_right never asserted: {state.get((LOW, CAN_SYS_SAFETY_STS))}"

    bench.send_lights()


def test_trail_braking_intent(auto_ready):
    """Cruise then brake 3000 kPa -> RT 0x205 intent rises, then releases."""
    bench = auto_ready
    bench.start_drive(1800, gear=GEAR_D)
    ok, _ = bench.wait_rate(LOW, CAN_RT_BRAKE_CMD, min_hz=10.0, timeout_s=4.0)
    assert ok, "RT 0x205 not streaming"

    bench.send_brake(3000)
    ok, state = bench.wait_signal(
        LOW, CAN_RT_BRAKE_CMD, "brake_pressure_kpa", expected=3000, timeout_s=3.0
    )
    assert ok, f"RT 0x205 did not show 3000 kPa: {state.get((LOW, CAN_RT_BRAKE_CMD))}"

    bench.send_brake(0)
    ok, _ = bench.wait_signal(
        LOW, CAN_RT_BRAKE_CMD, "brake_pressure_kpa", expected=0, timeout_s=3.0
    )
    assert ok, "RT 0x205 did not release"


def test_obstacle_deceleration(auto_ready):
    """Obstacle at 600 mm forces RT to cut the 0x204 speed setpoint."""
    bench = auto_ready
    bench.start_drive(1500, gear=GEAR_D)
    ok, _ = bench.wait_for(
        lambda s: abs((signal_of(s.get((LOW, CAN_RT_DRIVE_CMD)), "motor_speed_mmps") or 0) - 1500) <= 150,
        timeout_s=4.0,
    )
    assert ok, "cruise never established before obstacle test"

    bench.send_obstacle(600)
    ok, state = bench.wait_for(
        lambda s: (signal_of(s.get((LOW, CAN_RT_DRIVE_CMD)), "motor_speed_mmps") or 0) < 500,
        timeout_s=3.0,
    )
    assert ok, (
        f"RT 0x204 did not decelerate for a 600 mm obstacle: "
        f"{state.get((LOW, CAN_RT_DRIVE_CMD))}"
    )

    bench.send_obstacle(OBSTACLE_CLEAR)
