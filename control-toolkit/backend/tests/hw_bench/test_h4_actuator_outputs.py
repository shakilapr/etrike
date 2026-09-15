"""H4 — actuator output commands for the *absent* actuators (no mocks).

This is the core of the bench strategy: MTR/SES/SEB are not connected and their
feedback is **never synthesised**. Instead we verify the commands RT and SYS
produce for them:

    0x204 RT_DRIVE_CMD   -> MTR   (speed setpoint + gear)      [H3]
    0x205 RT_BRAKE_CMD   -> SYS   (brake intent, kPa)          [here]
    0x169 VCU_SES_REQ    -> SES   (steer angle request)        [here]
    0x7B9 VCU_SEB_REQ    -> SEB   (final brake command from SYS) [here]

Firmware facts these assertions rely on:
  * rt-esp32/src/steering_control.h — bench solo bypass skips SES listen-sync
    so 0x169 is transmitted immediately, tracking the yaw->steer resolver.
  * rt-esp32/src/main.cpp — 0x205 = clamp(max(obstacle_kpa, host_kpa), 0, 5000).
  * sys-esp32/src/brake_control.h — SYS is the sole normal 0x7B9 producer. With
    no SEB 0x721 status present it transmits the released 0 mm stroke (raw 600)
    at 50 Hz; on ESTOP it commands the 27 mm max stroke (raw 1140).
"""

from __future__ import annotations

import time

import pytest

from harness import (
    CAN_RT_BRAKE_CMD,
    CAN_RT_DRIVE_CMD,
    CAN_SES_REQ,
    CAN_SEB_REQ,
    CAN_SYS_NODE_STATUS,
    CAN_RT_NODE_STATUS,
    CAN_SYS_SAFETY_STS,
    GEAR_D,
    LOW,
    SEB_MODE_STROKE,
    SEB_STROKE_RAW_ZERO,
    signal_from,
    signal_of,
)

pytestmark = pytest.mark.hw_bench


def test_rt_brake_intent_idle_zero(auto_ready):
    """Cruising with no brake request -> RT 0x205 intent is 0 kPa."""
    bench = auto_ready
    bench.start_drive(1000, gear=GEAR_D)
    ok, _ = bench.wait_rate(LOW, CAN_RT_BRAKE_CMD, min_hz=10.0, timeout_s=4.0)
    assert ok, "RT 0x205 brake intent not streaming on Low"
    value = bench.signal(LOW, CAN_RT_BRAKE_CMD, "brake_pressure_kpa")
    assert value == 0, f"RT 0x205 idle brake intent != 0 ({value} kPa)"


def test_rt_brake_intent_tracks_host_request(auto_ready):
    """host_brake_req=3000 kPa -> RT 0x205 intent = 3000, then releases to 0."""
    bench = auto_ready
    bench.start_drive(1000, gear=GEAR_D)
    ok, _ = bench.wait_rate(LOW, CAN_RT_BRAKE_CMD, min_hz=10.0, timeout_s=4.0)
    assert ok, "RT 0x205 brake intent not streaming on Low"

    bench.send_brake(3000)
    ok, state = bench.wait_signal(
        LOW, CAN_RT_BRAKE_CMD, "brake_pressure_kpa", expected=3000, timeout_s=3.0
    )
    assert ok, f"RT 0x205 never reflected 3000 kPa: {state.get((LOW, CAN_RT_BRAKE_CMD))}"

    bench.send_brake(0)
    ok, state = bench.wait_signal(
        LOW, CAN_RT_BRAKE_CMD, "brake_pressure_kpa", expected=0, timeout_s=3.0
    )
    assert ok, f"RT 0x205 did not release back to 0 kPa: {state.get((LOW, CAN_RT_BRAKE_CMD))}"


def test_ses_req_streams_and_tracks_yaw(auto_ready):
    """RT 0x169 streams at 50 Hz and the target angle follows the yaw command.

    ``target_angle_raw`` carries a fixed vendor offset, so direction is checked
    relative to the straight-ahead baseline rather than against zero.
    """
    bench = auto_ready

    bench.start_drive(1500, yaw_rate_mrad_s=0, gear=GEAR_D)
    ok, _ = bench.wait_rate(LOW, CAN_SES_REQ, min_hz=10.0, timeout_s=5.0)
    assert ok, "RT 0x169 SES request not streaming at >=10 Hz on Low"
    ok, state = bench.wait_for(
        lambda s: signal_from(s, LOW, CAN_SES_REQ, "target_angle_raw") is not None,
        timeout_s=3.0,
    )
    assert ok, "RT 0x169 has no decoded target_angle_raw"
    straight = signal_from(state, LOW, CAN_SES_REQ, "target_angle_raw")

    bench.start_drive(1500, yaw_rate_mrad_s=400, gear=GEAR_D)
    ok, state = bench.wait_for(
        lambda s: signal_from(s, LOW, CAN_SES_REQ, "target_angle_raw") not in (None, straight),
        timeout_s=3.0,
    )
    assert ok, "RT 0x169 target angle did not respond to +yaw"
    pos = signal_from(state, LOW, CAN_SES_REQ, "target_angle_raw")

    bench.start_drive(1500, yaw_rate_mrad_s=-400, gear=GEAR_D)
    ok, state = bench.wait_for(
        lambda s: signal_from(s, LOW, CAN_SES_REQ, "target_angle_raw") not in (None, straight, pos),
        timeout_s=3.0,
    )
    assert ok, "RT 0x169 target angle did not respond to -yaw"
    neg = signal_from(state, LOW, CAN_SES_REQ, "target_angle_raw")

    assert abs(pos - straight) >= 20, f"+yaw barely moved the steer request (straight={straight}, pos={pos})"
    assert abs(neg - straight) >= 20, f"-yaw barely moved the steer request (straight={straight}, neg={neg})"
    assert (pos - straight) * (neg - straight) < 0, (
        f"+/- yaw did not steer in opposite directions (straight={straight}, pos={pos}, neg={neg})"
    )


def test_seb_req_streams_released_stroke(auto_ready):
    """SYS 0x7B9 streams at 50 Hz in released stroke mode without an SEB."""
    bench = auto_ready
    ok, _ = bench.wait_rate(LOW, CAN_SEB_REQ, min_hz=20.0, timeout_s=5.0)
    assert ok, "SYS 0x7B9 SEB request not streaming at >=20 Hz on Low"
    mode = bench.signal(LOW, CAN_SEB_REQ, "control_mode")
    stroke = bench.signal(LOW, CAN_SEB_REQ, "stroke_request_raw")
    assert mode == SEB_MODE_STROKE, f"SYS 0x7B9 control_mode != Stroke ({mode})"
    assert stroke == SEB_STROKE_RAW_ZERO, (
        f"SYS 0x7B9 released stroke != {SEB_STROKE_RAW_ZERO} (got {stroke}); "
        "no SEB status is present, so BrakeControl should hold 0 mm"
    )


def test_bypass_mode_holds_without_peers(auto_ready):
    """Directive: with MTR/SES/SEB absent, drive must NOT trip a safety stop.

    Streams a drive setpoint for several seconds with zero peer feedback and
    asserts both nodes stay healthy and RT keeps producing nonzero 0x204.
    """
    bench = auto_ready
    bench.start_drive(1000, yaw_rate_mrad_s=0, gear=GEAR_D)
    ok, _ = bench.wait_for(
        lambda s: abs((signal_of(s.get((LOW, CAN_RT_DRIVE_CMD)), "motor_speed_mmps") or 0) - 1000) <= 100,
        timeout_s=4.0,
    )
    assert ok, "RT 0x204 never reached the drive setpoint with peers absent"

    time.sleep(2.5)  # several heartbeats worth of missing SES/SEB/MTR feedback

    state = bench.state_map()
    estop = signal_of(state.get((LOW, CAN_SYS_SAFETY_STS)), "estop_active")
    assert estop == 0, "SYS latched ESTOP despite bench bypass mode"

    for bus, cid, label in (
        (LOW, CAN_RT_NODE_STATUS, "RT 0x501"),
        (LOW, CAN_SYS_NODE_STATUS, "SYS 0x500"),
    ):
        msg = state.get((bus, cid))
        assert msg is not None, f"{label} missing"
        assert signal_of(msg, "estop_active") == 0, f"{label} estop_active != 0"
        assert signal_of(msg, "degraded") == 0, f"{label} degraded != 0 (bypass should suppress peer-loss)"
        assert signal_of(msg, "block_mask") == 0, f"{label} block_mask != 0"

    speed = signal_of(state.get((LOW, CAN_RT_DRIVE_CMD)), "motor_speed_mmps")
    assert speed and speed > 0, f"RT 0x204 stopped producing motion with peers absent (speed={speed})"
