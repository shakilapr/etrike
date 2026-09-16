"""H8 — long real-time maneuver scenarios (~60 s each).

These are the "does it actually drive" tests: a scripted timeline of
high-level Host/HMI commands (accelerate, cruise, turn left/right, brake,
obstacle, stop, recover) played against the live bench while the low-level
unit command frames are sampled continuously. Every phase asserts:

  * the commanded setpoint is actually delivered to the right unit
    (0x204 MTR / 0x205 SYS / 0x169 SES / 0x7B9 SEB), and
  * **no node latches ESTOP** mid-maneuver (``scenario.hold``).

Durations scale with ``ETRIKE_BENCH_SCENARIO_SCALE`` (default 1.0 = real time),
so ``ETRIKE_BENCH_SCENARIO_SCALE=0.1 pytest -m slow`` gives a ~6 s smoke run.

MTR/SES/SEB are physically absent; feedback is never synthesised — these tests
only observe the commands RT/SYS produce for them.
"""

from __future__ import annotations

import math
import time

import pytest

from harness import (
    CAN_RT_NODE_STATUS,
    CAN_SYS_SAFETY_STS,
    GEAR_D,
    GEAR_N,
    HIGH,
    LOW,
    OBSTACLE_CLEAR,
    signal_of,
)
from scenario import (
    SBW_ANGLE_OFFSET,
    assert_no_trip,
    dur,
    health,
    hold,
    seb_pressure,
    speed_is,
    steer_gt,
    steer_lt,
    track,
)

pytestmark = [pytest.mark.hw_bench, pytest.mark.slow]


def test_commute_scenario(auto_ready):
    """A full commute: accelerate, cruise, turn both ways, brake, obstacle, stop."""
    bench = auto_ready

    # Accelerate and cruise straight.
    bench.start_drive(1500, yaw_rate_mrad_s=0, gear=GEAR_D)
    track(bench, speed_is(1500, tol=150), "commute: accelerate to 1500")
    hold(bench, "commute: cruise", 8)

    # Turn left, straighten, turn right.
    bench.start_drive(1500, yaw_rate_mrad_s=500, gear=GEAR_D)
    track(bench, steer_gt(SBW_ANGLE_OFFSET + 240), "commute: left turn")
    hold(bench, "commute: left turn holding", 5)

    bench.start_drive(1500, yaw_rate_mrad_s=0, gear=GEAR_D)
    hold(bench, "commute: straighten", 3)

    bench.start_drive(1500, yaw_rate_mrad_s=-500, gear=GEAR_D)
    track(bench, steer_lt(SBW_ANGLE_OFFSET - 240), "commute: right turn")
    hold(bench, "commute: right turn holding", 5)

    # Brake on the straight: 0x205 intent -> SYS Pressure mode.
    bench.start_drive(1500, yaw_rate_mrad_s=0, gear=GEAR_D)
    bench.send_brake(2500)
    track(bench, seb_pressure(), "commute: brake -> SEB pressure")
    hold(bench, "commute: braking", 4)
    bench.send_brake(0)
    hold(bench, "commute: brake release", 1)

    # Accelerate again, then handle an obstacle.
    bench.start_drive(2200, gear=GEAR_D)
    track(bench, speed_is(2200, tol=200), "commute: accelerate to 2200")
    hold(bench, "commute: fast cruise", 8)

    bench.send_obstacle(400)
    track(bench, speed_is(0, tol=500), "commute: obstacle slows the setpoint")
    hold(bench, "commute: obstacle held", 4)
    bench.send_obstacle(OBSTACLE_CLEAR)
    hold(bench, "commute: obstacle cleared", 1)

    # Stop neutral.
    bench.start_drive(0, gear=GEAR_N)
    track(bench, speed_is(0, tol=5), "commute: stop")
    hold(bench, "commute: stopped", 3)


def test_stop_and_go_scenario(auto_ready):
    """Six accelerate/brake cycles: the stop-and-go traffic pattern."""
    bench = auto_ready
    for cycle in range(6):
        bench.start_drive(1200, gear=GEAR_D)
        track(bench, speed_is(1200, tol=150), f"stop-go[{cycle}]: accelerate")
        hold(bench, f"stop-go[{cycle}]: cruise", 2)

        bench.send_brake(3000)
        track(bench, seb_pressure(), f"stop-go[{cycle}]: brake")
        hold(bench, f"stop-go[{cycle}]: braked", 2)

        bench.send_brake(0)
        hold(bench, f"stop-go[{cycle}]: release", 1)


def test_slalom_scenario(auto_ready):
    """Cruise while alternating left/right steering eight times."""
    bench = auto_ready
    bench.start_drive(1200, yaw_rate_mrad_s=0, gear=GEAR_D)
    track(bench, speed_is(1200, tol=150), "slalom: cruise")

    for index in range(8):
        right = index % 2 == 1
        yaw = -500 if right else 500
        bench.start_drive(1200, yaw_rate_mrad_s=yaw, gear=GEAR_D)
        if right:
            track(bench, steer_lt(SBW_ANGLE_OFFSET - 240), f"slalom[{index}]: right")
        else:
            track(bench, steer_gt(SBW_ANGLE_OFFSET + 240), f"slalom[{index}]: left")
        hold(bench, f"slalom[{index}]", 1.5)

    bench.start_drive(0, yaw_rate_mrad_s=0, gear=GEAR_N)
    track(bench, speed_is(0, tol=5), "slalom: stop")


def test_turn_brake_combinations_scenario(auto_ready):
    """Braking into and out of a turn must keep both units coherent."""
    bench = auto_ready
    for cycle in range(4):
        bench.start_drive(1600, yaw_rate_mrad_s=450, gear=GEAR_D)
        track(bench, steer_gt(SBW_ANGLE_OFFSET + 200), f"combo[{cycle}]: turn in")
        track(bench, speed_is(1600, tol=200), f"combo[{cycle}]: at speed")
        hold(bench, f"combo[{cycle}]: cornering", 3)

        bench.send_brake(2000)
        track(bench, seb_pressure(), f"combo[{cycle}]: brake in corner")
        hold(bench, f"combo[{cycle}]: braked corner", 3)

        bench.send_brake(0)
        bench.start_drive(1600, yaw_rate_mrad_s=-450, gear=GEAR_D)
        track(bench, steer_lt(SBW_ANGLE_OFFSET - 200), f"combo[{cycle}]: turn out")
        hold(bench, f"combo[{cycle}]: opposite corner", 3)

    bench.start_drive(0, yaw_rate_mrad_s=0, gear=GEAR_N)
    track(bench, speed_is(0, tol=5), "combo: stop")


def test_estop_recovery_cycles_scenario(auto_ready):
    """Repeated trip -> safe-outputs -> staged-reset -> re-arm cycles."""
    bench = auto_ready
    for cycle in range(3):
        bench.start_drive(1000, gear=GEAR_D)
        track(bench, speed_is(1000, tol=150), f"estop-cycle[{cycle}]: drive")

        assert bench.trip_estop(HIGH), f"estop-cycle[{cycle}]: SYS/RT never latched ESTOP"
        ok, state = bench.wait_signal(
            LOW, CAN_SYS_SAFETY_STS, "estop_active", expected=1, timeout_s=2.0
        )
        assert ok, (
            f"estop-cycle[{cycle}]: SYS never reported ESTOP: "
            f"{state.get((LOW, CAN_SYS_SAFETY_STS))}"
        )
        # RT must latch too (this is an intentional trip, so no hold()).
        ok, state = bench.wait_for(
            lambda s: signal_of(s.get((LOW, CAN_RT_NODE_STATUS)), "estop_active") == 1,
            timeout_s=3.0,
        )
        assert ok, f"estop-cycle[{cycle}]: RT never latched ESTOP"

        assert bench.reset_estop(), f"estop-cycle[{cycle}]: staged reset never cleared ESTOP"

        # Reset exits ESTOP to MANUAL and the steering machine ramps out before
        # motion authority returns, so re-arm + re-drive with a few retries.
        engaged = False
        for _ in range(3):
            bench.command_mode(True)
            bench.start_drive(1000, gear=GEAR_D)
            engaged, _ = bench.wait_for(speed_is(1000, tol=150), timeout_s=3.0)
            if engaged:
                break
        assert engaged, f"estop-cycle[{cycle}]: drive did not re-engage after recovery"

    bench.start_drive(0, gear=GEAR_N)


def test_endurance_varied_drive_scenario(auto_ready):
    """~60 s of continuously varied speed/steer/brake with per-sample invariants."""
    bench = auto_ready
    bench.start_drive(1400, gear=GEAR_D)
    track(bench, speed_is(1400, tol=200), "endurance: engage")

    start = time.monotonic()
    total = dur(60)
    phase = 0
    missing = 0
    while time.monotonic() - start < total:
        elapsed = time.monotonic() - start
        # Sweep the steering sinusoidally and brake for the last 5 s of each 20 s.
        yaw = int(450 * math.sin(elapsed / 3.0))
        braking = (int(elapsed) % 20) >= 15
        bench.start_drive(1400, yaw_rate_mrad_s=yaw, gear=GEAR_D)
        bench.send_brake(1800 if braking else 0)
        if not braking:
            bench.send_obstacle(OBSTACLE_CLEAR)

        snapshot = health(bench)
        if snapshot["low_live"]:
            missing = 0
            assert_no_trip(snapshot, "endurance")
            assert snapshot["rt_degraded"] == 0, f"endurance: RT degraded {snapshot}"
            task_health = snapshot["task_health"]
            assert (task_health & 0x0F) == 0x0F, (
                f"endurance: RT task_health lost a task: {snapshot}"
            )
        else:
            missing += 1
            assert missing <= 6, (
                f"endurance: Low-bus telemetry missing for {missing} samples "
                f"(adapter reconnect?): {snapshot}"
            )
        phase += 1
        time.sleep(0.5)

    assert phase > 0, "endurance scenario sampled no phases (scale too small?)"

    bench.send_brake(0)
    bench.send_obstacle(OBSTACLE_CLEAR)
    bench.start_drive(0, yaw_rate_mrad_s=0, gear=GEAR_N)
    track(bench, speed_is(0, tol=5), "endurance: stop")
