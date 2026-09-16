"""H9 — robustness, negative, and stress command paths.

Unlike H7 (one command -> one unit) and H8 (real-time timelines), these tests
poke the edges: lose a stream mid-maneuver, toggle modes quickly, repeat
obstacle/brake arbitration, and confirm unrelated commands do not disturb the
actuator command frames.
"""

from __future__ import annotations

import time

import pytest

from harness import (
    GEAR_D,
    OBSTACLE_CLEAR,
)
from scenario import (
    SBW_ANGLE_OFFSET,
    assert_no_trip,
    brake_above,
    brake_is,
    health,
    hold,
    speed_is,
    steer_gt,
    track,
)

pytestmark = pytest.mark.hw_bench


def test_host_stream_loss_during_turn_zeroes_setpoint(auto_ready):
    """Losing 0x300 mid-turn zeroes 0x204 without latching ESTOP, then resumes."""
    bench = auto_ready
    job = bench.start_drive(1200, yaw_rate_mrad_s=450, gear=GEAR_D)
    assert job, "failed to start periodic host drive stream"
    track(bench, speed_is(1200, tol=150), "turn-loss: engage")
    track(bench, steer_gt(SBW_ANGLE_OFFSET + 200), "turn-loss: steering")

    bench.cancel_injection(job)
    track(bench, speed_is(0, tol=5), "turn-loss: setpoint zeroed", timeout_s=3.0)
    snapshot = health(bench)
    assert_no_trip(snapshot, "turn-loss")

    bench.start_drive(1200, gear=GEAR_D)
    track(bench, speed_is(1200, tol=150), "turn-loss: stream resumed")


def test_rapid_mode_toggle_is_stable(auto_ready):
    """Fast MANUAL<->AUTO toggling must not fault either node."""
    bench = auto_ready
    for index in range(6):
        ok, _ = bench.command_mode(auto=index % 2 == 0)
        assert ok, f"mode toggle {index} did not apply"

    ok, _ = bench.command_mode(True)
    assert ok, "could not settle back to AUTO"

    bench.start_drive(800, gear=GEAR_D)
    track(bench, speed_is(800, tol=150), "toggle: drive after toggles")
    hold(bench, "toggle: settled", 1)


def test_obstacle_appear_clear_cycles(auto_ready):
    """An obstacle that repeatedly appears and clears raises then releases the brake."""
    bench = auto_ready
    bench.start_drive(1500, gear=GEAR_D)
    track(bench, speed_is(1500, tol=150), "obstacle: cruise")

    for index in range(6):
        bench.send_obstacle(350)
        track(bench, brake_above(0), f"obstacle[{index}]: brake intent rises")
        hold(bench, f"obstacle[{index}]: held", 1)

        bench.send_obstacle(OBSTACLE_CLEAR)
        track(bench, brake_is(0), f"obstacle[{index}]: brake released")
        hold(bench, f"obstacle[{index}]: cleared", 1)

    bench.start_drive(0, gear=GEAR_D)


def test_brake_arbitration_obstacle_beats_host(auto_ready):
    """0x205 = max(obstacle, host): the larger demand wins, then falls back."""
    bench = auto_ready
    bench.start_drive(1200, gear=GEAR_D)

    bench.send_brake(1000)
    track(bench, brake_is(1000, tol=50), "arb: host brake applied")

    bench.send_obstacle(400)  # ~4.8 MPa obstacle demand
    track(bench, brake_above(1000), "arb: obstacle wins over host")

    bench.send_obstacle(OBSTACLE_CLEAR)
    track(bench, brake_is(1000, tol=50), "arb: falls back to host demand")

    bench.send_brake(0)
    track(bench, brake_is(0), "arb: released")


def test_lights_do_not_disturb_drive(auto_ready):
    """Light commands must not affect the drive command stream or trip safety."""
    bench = auto_ready
    bench.start_drive(1000, gear=GEAR_D)
    track(bench, speed_is(1000, tol=150), "lights: cruise")

    for brake, head in ((1, 1), (0, 1), (1, 0), (0, 0)):
        bench.send_lights(brake=brake, head=head)
        time.sleep(0.4)
        snapshot = health(bench)
        assert_no_trip(snapshot, "lights")
        assert abs((snapshot["speed"] or 0) - 1000) <= 150, (
            f"light command disturbed the drive setpoint: {snapshot}"
        )


def test_power_cycle_recovers(auto_ready):
    """Power OFF -> ON returns the bench to a drivable AUTO state."""
    bench = auto_ready
    bench.start_drive(800, gear=GEAR_D)
    track(bench, speed_is(800, tol=150), "power: drive before cycle")

    ok, _ = bench.command_power(False)
    assert ok, "SYS_PWR_CMD never reported power_state=0"

    ok, _ = bench.command_power(True)
    assert ok, "SYS_PWR_CMD never reported power_state=1 after ON"

    ok, _ = bench.command_mode(True)
    assert ok, "could not re-arm AUTO after the power cycle"

    bench.start_drive(800, gear=GEAR_D)
    track(bench, speed_is(800, tol=150), "power: drive after cycle")
