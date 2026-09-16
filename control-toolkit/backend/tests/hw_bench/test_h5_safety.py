"""H5 — ESTOP trip, safe outputs, and staged reset recovery.

Covers handoff Step 3. A CAN ``0x001`` ESTOP latches SYS and RT; with the bench
MTR-absent bypass the host reset request is now accepted, so the full trip ->
recover cycle is reversible and runs by default.

Reset is inherently multi-frame: SYS ``StreamValidity`` only restores authority
on the second counter-advancing ``0x114`` frame, and RT independently requires
two counter-advancing ``0x011 estop_active=0`` frames before it drops its latch.
"""

from __future__ import annotations

import pytest

from harness import (
    CAN_ESTOP_RESET_RSP,
    CAN_RT_DRIVE_CMD,
    CAN_RT_NODE_STATUS,
    CAN_SEB_REQ,
    CAN_SYS_PWR_CMD,
    CAN_SYS_SAFETY_STS,
    GEAR_D,
    GEAR_N,
    HIGH,
    LOW,
    SEB_STROKE_RAW_ESTOP_MAX,
    signal_of,
)

pytestmark = pytest.mark.hw_bench


def _trip_and_assert_safe_outputs(bench, trip_bus: str):
    """Engage motion, trip ESTOP on ``trip_bus``, assert safe outputs."""
    bench.start_drive(1000, gear=GEAR_D)
    ok, _ = bench.wait_for(
        lambda s: abs((signal_of(s.get((LOW, CAN_RT_DRIVE_CMD)), "motor_speed_mmps") or 0) - 1000) <= 100,
        timeout_s=4.0,
    )
    assert ok, "drive never engaged before the ESTOP trip"

    assert bench.assert_estop(trip_bus), f"raw 0x001 injection failed on {trip_bus}"
    ok, state = bench.wait_signal(
        LOW, CAN_SYS_SAFETY_STS, "estop_active", expected=1, timeout_s=4.0
    )
    assert ok, f"SYS 0x011 never reported estop_active=1 after 0x001 on {trip_bus}"

    # RT zeroes the drive command and reports ESTOP on its node status.
    ok, state = bench.wait_for(
        lambda s: signal_of(s.get((LOW, CAN_RT_DRIVE_CMD)), "motor_speed_mmps") == 0
        and signal_of(s.get((LOW, CAN_RT_DRIVE_CMD)), "gear") == GEAR_N
        and signal_of(s.get((LOW, CAN_RT_NODE_STATUS)), "estop_active") == 1,
        timeout_s=3.0,
    )
    assert ok, (
        "RT did not zero/neutral 0x204 and report ESTOP: "
        f"0x204={state.get((LOW, CAN_RT_DRIVE_CMD))}, "
        f"0x501={state.get((LOW, CAN_RT_NODE_STATUS))}"
    )

    # SYS is the authoritative final SEB command: it must command max stroke.
    # (RT's 0x205 intent is not asserted here: RT stops transmitting it once the
    # mode authority drops to MANUAL during ESTOP.)
    ok, state = bench.wait_signal(
        LOW, CAN_SEB_REQ, "stroke_request_raw", expected=SEB_STROKE_RAW_ESTOP_MAX, timeout_s=3.0
    )
    assert ok, (
        f"SYS 0x7B9 ESTOP stroke != {SEB_STROKE_RAW_ESTOP_MAX}: {state.get((LOW, CAN_SEB_REQ))}"
    )


def _assert_recovered(bench):
    """Staged reset must clear ESTOP and restore power/applied motion."""
    assert bench.reset_estop(), "ESTOP never cleared after the staged reset sequence"

    # F7: the staged reset reply must itself report success — ACCEPTED (result=0)
    # with no outstanding blocker. This locks the 2-frame 0x114 -> 0x115 contract.
    ok, state = bench.wait_signal(LOW, CAN_ESTOP_RESET_RSP, "result", expected=0, timeout_s=3.0)
    assert ok, (
        "SYS 0x115 reset result was not ACCEPTED (0): "
        f"{state.get((LOW, CAN_ESTOP_RESET_RSP))}"
    )
    ok, state = bench.wait_signal(
        LOW, CAN_ESTOP_RESET_RSP, "blocker_mask", expected=0, timeout_s=3.0
    )
    assert ok, (
        "SYS 0x115 blocker_mask != 0 on an accepted reset: "
        f"{state.get((LOW, CAN_ESTOP_RESET_RSP))}"
    )

    ok, state = bench.wait_for(
        lambda s: signal_of(s.get((LOW, CAN_SYS_SAFETY_STS)), "estop_active") == 0
        and signal_of(s.get((LOW, CAN_RT_NODE_STATUS)), "estop_active") == 0,
        timeout_s=6.0,
    )
    assert ok, "ESTOP did not clear on both nodes after reset"

    ok, state = bench.wait_signal(LOW, CAN_SYS_PWR_CMD, "power_state", expected=1, timeout_s=4.0)
    assert ok, f"SYS 0x113 did not restore power after reset: {state.get((LOW, CAN_SYS_PWR_CMD))}"

    # Reset exits ESTOP to MANUAL; re-arm AUTO before checking motion authority.
    ok_mode, _ = bench.command_mode(True)
    assert ok_mode, "did not return to AUTO after ESTOP recovery"

    # Motion authority must be usable again.
    bench.start_drive(800, gear=GEAR_D)
    ok, state = bench.wait_for(
        lambda s: abs((signal_of(s.get((LOW, CAN_RT_DRIVE_CMD)), "motor_speed_mmps") or 0) - 800) <= 100,
        timeout_s=4.0,
    )
    assert ok, f"drive did not re-engage after recovery: {state.get((LOW, CAN_RT_DRIVE_CMD))}"


def test_high_bus_estop_trip_and_recovery(auto_ready):
    """0x001 on High propagates to both nodes; staged reset recovers to STANDBY."""
    _trip_and_assert_safe_outputs(auto_ready, HIGH)
    _assert_recovered(auto_ready)


def test_low_bus_estop_trip_and_recovery(auto_ready):
    """0x001 on Low latches both nodes; staged reset recovers to STANDBY."""
    _trip_and_assert_safe_outputs(auto_ready, LOW)
    _assert_recovered(auto_ready)
