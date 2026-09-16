"""H7 — high-level command -> low-level unit verification matrix.

Each test injects a *high-level* command (Host/HMI) and asserts what the
**low-level unit command frames** carry, since MTR/SES/SEB are physically
absent and their feedback is never synthesised:

    0x204 RT_DRIVE_CMD  -> MTR      (speed setpoint + gear)
    0x205 RT_BRAKE_CMD  -> SYS      (brake intent, kPa)
    0x169 VCU_SES_REQ   -> SES/SES(steer angle + slew + control enables)
    0x7B9 VCU_SEB_REQ   -> SEB      (final stroke/pressure command from SYS)
    0x011 SYS_SAFETY_STS-> lamps     (brake/head/turn bits)

Firmware facts these assertions pin (verified in source):
  * 0x303 HOST_STEER_CMD drives RT's direct-steering path:
    ``steer_angle_mdeg = angle_0_1deg * 100`` (phase2_motion.h) and
    ``0x169.target_angle_raw = angle_0_1deg + kSbwAngleOffset(=30000)``
    (steering_control.h); a direct command goes stale after 100 ms.
  * 0x169 is complete when ``control_enable=1``, ``alignment_enable=1`` and
    ``target_speed_raw`` is the speed-scaled slew in [125, 525] deg/s.
  * 0x205 = clamp(max(obstacle_kpa, host_kpa), 0, 5000).
  * 0x7B9 pressure raw = (kPa + 25) / 50 (integer), clamped to 100; released
    stroke raw = 600, ESTOP max stroke = 1140.
  * SYS forces the brake lamp in ESTOP and follows the CAN brake/head bits in
    AUTO (light_control.h). The steering ESTOP machine ramps -> holds -> goes
    silent, so 0x169 must stop after a trip.

Not host-observable (documented, not tested here):
  * the physical lamp drivers, the brake lever, and real MTR/SES/SEB motion;
  * the physical-e-stop reset blocker (GPIO1) — needs the e-stop loop opened.
"""

from __future__ import annotations

import time

import pytest

from harness import (
    CAN_RT_BRAKE_CMD,
    CAN_RT_DRIVE_CMD,
    CAN_SEB_REQ,
    CAN_SES_REQ,
    CAN_SYS_SAFETY_STS,
    GEAR_D,
    GEAR_N,
    GEAR_R,
    GEAR_S,
    HIGH,
    LOW,
    OBSTACLE_CLEAR,
    SEB_MODE_PRESSURE,
    SEB_MODE_STROKE,
    SEB_STROKE_RAW_ZERO,
    is_live,
    signal_of,
)

pytestmark = pytest.mark.hw_bench

# steering_control.h dynamic slew bounds (deg/s) and sbw raw offset.
STEER_SLEW_MIN = 125
STEER_SLEW_MAX = 525
SBW_ANGLE_OFFSET = 30000  # 0 deg -> raw 30000
MAX_BRAKE_KPA = 5000
SEB_PRESSURE_DIV = 50


def _val(bench, cid, name, default=None):
    return signal_of(bench.message(LOW, cid), name, default)


def _rolling_counter_advances(bench, cid, timeout_s: float = 1.0) -> bool:
    """True once the message's rolling_counter differs across two samples."""
    deadline = time.monotonic() + timeout_s
    first = _val(bench, cid, "rolling_counter")
    if first is None:
        return False
    while time.monotonic() < deadline:
        time.sleep(0.05)
        now = _val(bench, cid, "rolling_counter")
        if now is not None and now != first:
            return True
    return False


def _send_lights_until(bench, *, left=0, right=0, brake=0, head=0, timeout_s: float = 5.0):
    """Best-effort High->Low light forward (single bounded gateway frame)."""
    deadline = time.monotonic() + timeout_s
    state = {}
    fields = (("light_left", left), ("light_right", right),
              ("light_brake", brake), ("light_head", head))
    while time.monotonic() < deadline:
        bench.send_lights(left=left, right=right, brake=brake, head=head)
        state = bench.state_map()
        if all(signal_of(state.get((LOW, CAN_SYS_SAFETY_STS)), f) == v for f, v in fields):
            return True, state
        time.sleep(0.1)
    return False, state


# ── MTR unit (0x204) ─────────────────────────────────────────────────────
def test_mtr_gear_field_covers_all_gears(auto_ready):
    """Every commanded gear is reflected on 0x204 with the matching speed sign."""
    bench = auto_ready
    for gear, speed in ((GEAR_D, 800), (GEAR_S, 800), (GEAR_R, -500), (GEAR_N, 0)):
        bench.start_drive(speed, gear=gear)
        ok, state = bench.wait_for(
            lambda s, g=gear, sp=speed: (
                signal_of(s.get((LOW, CAN_RT_DRIVE_CMD)), "gear") == g
                and abs((signal_of(s.get((LOW, CAN_RT_DRIVE_CMD)), "motor_speed_mmps") or 0) - sp) <= 100
            ),
            timeout_s=4.0,
        )
        assert ok, (
            f"RT 0x204 did not reach gear={gear} speed={speed}: "
            f"{state.get((LOW, CAN_RT_DRIVE_CMD))}"
        )


def test_mtr_speed_tracks_contract_max(auto_ready):
    """A 3000 mm/s host request (contract max) is passed to MTR unchanged."""
    bench = auto_ready
    bench.start_drive(3000, gear=GEAR_D)
    ok, state = bench.wait_for(
        lambda s: abs((signal_of(s.get((LOW, CAN_RT_DRIVE_CMD)), "motor_speed_mmps") or 0) - 3000) <= 150,
        timeout_s=4.0,
    )
    assert ok, f"RT 0x204 did not track 3000 mm/s: {state.get((LOW, CAN_RT_DRIVE_CMD))}"


def test_mtr_drive_latency_bounded(auto_ready):
    """A host setpoint step reaches 0x204 within a bounded latency."""
    bench = auto_ready
    bench.start_drive(400, gear=GEAR_D)
    ok, _ = bench.wait_for(
        lambda s: abs((signal_of(s.get((LOW, CAN_RT_DRIVE_CMD)), "motor_speed_mmps") or 0) - 400) <= 100,
        timeout_s=4.0,
    )
    assert ok, "baseline 400 mm/s never established"

    bench.start_drive(1600, gear=GEAR_D)
    ok, state = bench.wait_for(
        lambda s: abs((signal_of(s.get((LOW, CAN_RT_DRIVE_CMD)), "motor_speed_mmps") or 0) - 1600) <= 100,
        timeout_s=1.5,
    )
    assert ok, (
        "RT 0x204 step latency exceeded 1.5 s: "
        f"{state.get((LOW, CAN_RT_DRIVE_CMD))}"
    )


# ── SYS brake intent unit (0x205) ────────────────────────────────────────
def test_brake_intent_clamps_at_max(auto_ready):
    """host_brake_req above 5 MPa is clamped: 0x205 never exceeds 5000 kPa."""
    bench = auto_ready
    bench.start_drive(1000, gear=GEAR_D)

    bench.send_brake(20000)  # contract max; RT clamps to 5000
    ok, state = bench.wait_signal(
        LOW, CAN_RT_BRAKE_CMD, "brake_pressure_kpa", expected=MAX_BRAKE_KPA, timeout_s=3.0
    )
    assert ok, (
        f"RT 0x205 did not clamp 20000 kPa to {MAX_BRAKE_KPA}: "
        f"{state.get((LOW, CAN_RT_BRAKE_CMD))}"
    )

    bench.send_brake(0)
    ok, _ = bench.wait_signal(LOW, CAN_RT_BRAKE_CMD, "brake_pressure_kpa", expected=0, timeout_s=3.0)
    assert ok, "RT 0x205 did not release to 0 kPa"


# ── SES/EPS unit (0x169) ─────────────────────────────────────────────────
def test_ses_command_is_complete(auto_ready):
    """0x169 carries the full steer command: enables + speed-scaled slew."""
    bench = auto_ready
    bench.start_drive(1500, yaw_rate_mrad_s=0, gear=GEAR_D)
    ok, state = bench.wait_rate(LOW, CAN_SES_REQ, min_hz=10.0, timeout_s=5.0)
    assert ok, "RT 0x169 not streaming"

    msg = state.get((LOW, CAN_SES_REQ))
    assert signal_of(msg, "control_enable") == 1, f"0x169 control_enable != 1: {msg}"
    assert signal_of(msg, "alignment_enable") == 1, f"0x169 alignment_enable != 1: {msg}"
    slew = signal_of(msg, "target_speed_raw")
    assert slew is not None and STEER_SLEW_MIN <= slew <= STEER_SLEW_MAX, (
        f"0x169 target_speed_raw {slew} outside [{STEER_SLEW_MIN}, {STEER_SLEW_MAX}] deg/s"
    )


def test_direct_steer_tracks_host_steer(auto_ready):
    """host_steer_cmd (0x303) direct-steers 0x169 with the commanded sign."""
    bench = auto_ready
    bench.start_drive(600, yaw_rate_mrad_s=0, gear=GEAR_D)
    ok, state = bench.wait_for(
        lambda s: signal_of(s.get((LOW, CAN_SES_REQ)), "target_angle_raw") is not None,
        timeout_s=4.0,
    )
    assert ok, "0x169 target_angle_raw never decoded"
    baseline = signal_of(state.get((LOW, CAN_SES_REQ)), "target_angle_raw")
    assert abs(baseline - SBW_ANGLE_OFFSET) <= 150, (
        f"straight-ahead 0x169 target {baseline} != offset {SBW_ANGLE_OFFSET}"
    )

    job = bench.start_steer(300)  # +30.0 deg
    assert job, "failed to start periodic host_steer_cmd"
    ok, state = bench.wait_for(
        lambda s: (signal_of(s.get((LOW, CAN_SES_REQ)), "target_angle_raw") or 0)
        >= SBW_ANGLE_OFFSET + 240,
        timeout_s=3.0,
    )
    assert ok, f"0x169 did not follow +30.0 deg: {state.get((LOW, CAN_SES_REQ))}"
    bench.cancel_injection(job)

    job = bench.start_steer(-300)  # -30.0 deg
    assert job, "failed to start periodic host_steer_cmd"
    ok, state = bench.wait_for(
        lambda s: (signal_of(s.get((LOW, CAN_SES_REQ)), "target_angle_raw") or 0)
        <= SBW_ANGLE_OFFSET - 240,
        timeout_s=3.0,
    )
    assert ok, f"0x169 did not follow -30.0 deg: {state.get((LOW, CAN_SES_REQ))}"
    bench.cancel_injection(job)


# ── SEB unit (0x7B9) ─────────────────────────────────────────────────────
def test_seb_pressure_scaling(auto_ready):
    """SYS maps the 0x205 kPa intent into the SEB pressure raw across the range."""
    bench = auto_ready
    ok, _ = bench.wait_rate(LOW, CAN_SEB_REQ, min_hz=20.0, timeout_s=5.0)
    assert ok, "SYS 0x7B9 not streaming"

    for kpa in (1000, 2000, 3000):
        expected = (kpa + SEB_PRESSURE_DIV // 2) // SEB_PRESSURE_DIV
        bench.send_brake(kpa)
        ok, state = bench.wait_for(
            lambda s, e=expected: signal_of(s.get((LOW, CAN_SEB_REQ)), "control_mode") == SEB_MODE_PRESSURE
            and signal_of(s.get((LOW, CAN_SEB_REQ)), "pressure_request_raw") == e,
            timeout_s=3.0,
        )
        assert ok, (
            f"SYS 0x7B9 did not map {kpa} kPa -> Pressure/{expected}: "
            f"{state.get((LOW, CAN_SEB_REQ))}"
        )
        assert signal_of(state.get((LOW, CAN_SEB_REQ)), "auto_brake") == 1

    bench.send_brake(0)
    ok, state = bench.wait_for(
        lambda s: signal_of(s.get((LOW, CAN_SEB_REQ)), "control_mode") == SEB_MODE_STROKE
        and signal_of(s.get((LOW, CAN_SEB_REQ)), "stroke_request_raw") == SEB_STROKE_RAW_ZERO,
        timeout_s=3.0,
    )
    assert ok, f"SYS 0x7B9 did not release to Stroke/{SEB_STROKE_RAW_ZERO}: {state.get((LOW, CAN_SEB_REQ))}"


def test_obstacle_escalates_brake_to_seb(auto_ready):
    """A near obstacle raises 0x205 and SYS relays it to the SEB pressure command."""
    bench = auto_ready
    bench.start_drive(1500, yaw_rate_mrad_s=0, gear=GEAR_D)
    ok, _ = bench.wait_for(
        lambda s: abs((signal_of(s.get((LOW, CAN_RT_DRIVE_CMD)), "motor_speed_mmps") or 0) - 1500) <= 150,
        timeout_s=4.0,
    )
    assert ok, "cruise never established before obstacle test"

    bench.send_obstacle(400)
    ok, state = bench.wait_for(
        lambda s: (signal_of(s.get((LOW, CAN_RT_BRAKE_CMD)), "brake_pressure_kpa") or 0) > 0,
        timeout_s=3.0,
    )
    assert ok, f"600 mm obstacle did not raise 0x205 brake intent: {state.get((LOW, CAN_RT_BRAKE_CMD))}"

    kpa = signal_of(state.get((LOW, CAN_RT_BRAKE_CMD)), "brake_pressure_kpa")
    expected_raw = (int(kpa) + SEB_PRESSURE_DIV // 2) // SEB_PRESSURE_DIV
    ok, state = bench.wait_for(
        lambda s, e=expected_raw: signal_of(s.get((LOW, CAN_SEB_REQ)), "control_mode") == SEB_MODE_PRESSURE
        and signal_of(s.get((LOW, CAN_SEB_REQ)), "pressure_request_raw") == e,
        timeout_s=3.0,
    )
    assert ok, (
        f"SYS 0x7B9 did not relay obstacle brake ({kpa} kPa -> raw {expected_raw}): "
        f"{state.get((LOW, CAN_SEB_REQ))}"
    )

    bench.send_obstacle(OBSTACLE_CLEAR)


def test_low_level_frames_e2e_and_counters(auto_ready):
    """All low-level commands carry a valid E2E frame and live counters."""
    bench = auto_ready
    bench.start_drive(1000, gear=GEAR_D)
    ok, state = bench.wait_rate(LOW, CAN_RT_BRAKE_CMD, min_hz=10.0, timeout_s=4.0)
    assert ok, "RT 0x205 not streaming"
    ok, _ = bench.wait_rate(LOW, CAN_SEB_REQ, min_hz=10.0, timeout_s=4.0)
    assert ok, "SYS 0x7B9 not streaming"

    for cid, label in ((CAN_RT_DRIVE_CMD, "0x204"), (CAN_SES_REQ, "0x169"), (CAN_SEB_REQ, "0x7B9")):
        msg = bench.message(LOW, cid)
        assert msg is not None, f"{label} missing on Low"
        assert msg.get("validation_status") == "ok", f"{label} E2E validation != ok: {msg.get('validation_status')}"

    assert _rolling_counter_advances(bench, CAN_SES_REQ), "0x169 rolling_counter is not advancing"
    assert _rolling_counter_advances(bench, CAN_SEB_REQ), "0x7B9 rolling_counter is not advancing"


# ── SYS lamp outputs (0x011) ─────────────────────────────────────────────
def test_brake_and_head_lamps_follow_host(auto_ready):
    """HOST_LIGHT_CMD brake/head bits reach SYS lamp outputs (0x011)."""
    bench = auto_ready
    ok, state = _send_lights_until(bench, brake=1, head=1)
    assert ok, f"SYS 0x011 brake/head not asserted: {state.get((LOW, CAN_SYS_SAFETY_STS))}"

    ok, state = _send_lights_until(bench)
    assert ok, f"SYS 0x011 brake/head did not clear: {state.get((LOW, CAN_SYS_SAFETY_STS))}"


# ── Safety-path command outputs ──────────────────────────────────────────
def test_estop_silences_steer_and_forces_brake_lamp(auto_ready):
    """ESTOP trips safe outputs: SES request goes silent and the brake lamp is on."""
    bench = auto_ready
    bench.start_drive(1000, gear=GEAR_D)
    ok, _ = bench.wait_rate(LOW, CAN_SES_REQ, min_hz=10.0, timeout_s=5.0)
    assert ok, "RT 0x169 not streaming before the ESTOP trip"

    assert bench.assert_estop(HIGH), "raw 0x001 injection failed"
    ok, state = bench.wait_signal(LOW, CAN_SYS_SAFETY_STS, "estop_active", expected=1, timeout_s=4.0)
    assert ok, "SYS never reported ESTOP"

    # light_control.h forces the brake lamp while in ESTOP.
    ok, state = bench.wait_signal(LOW, CAN_SYS_SAFETY_STS, "light_brake", expected=1, timeout_s=3.0)
    assert ok, f"SYS brake lamp not forced in ESTOP: {state.get((LOW, CAN_SYS_SAFETY_STS))}"

    # steering ESTOP machine ramps -> holds -> silent: 0x169 must stop.
    ok, _ = bench.wait_for(lambda s: not is_live(s.get((LOW, CAN_SES_REQ))), timeout_s=6.0)
    assert ok, "RT 0x169 did not go silent after the ESTOP trip"
