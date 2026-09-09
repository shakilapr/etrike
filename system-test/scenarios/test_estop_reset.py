"""Scenario: DISTRIBUTED ESTOP -> operator reset -> REARM (milestone 3).

The exact external sequence from the L9 plan:

    boot healthy -> AUTO -> drive 1500 -> PRESS physical ESTOP
        -> 0x001, 0x011 estop=1, 0x113=OFF, RT 0x204=0, MTR estop flag
    RELEASE ESTOP + keep AUTO/drive 10 s -> traction MUST NEVER resume
    PRESS START -> two valid advancing 0x011 clear frames
        -> RT clears, MTR clears, MTR STILL not driving
    power OFF->ON (0x113 fresh epoch) -> fresh AUTO+drive -> traction resumes

Maps to native matrix tests 1-5 (echo/spam/loop) and 36-50 (clear-frame +
rearm rules) but executed against the *real* ECUs over the physical bus.
"""
from __future__ import annotations

import time

import pytest

from assertions import safety
from assertions.temporal import never_window, wait_for_valid
from bench.wire import (
    LOW,
    MTR_NODE_STATUS,
    RT_DRIVE_CMD,
    SYS_PWR_CMD,
    SYS_SAFETY_STS,
    encode_message,
    safety_sts_e2e_ok,
)
from protocol.e2e import sys_safety_sts_crc
from scenarios.procedures import (
    count_valid_clear_frames,
    operator_start_reset,
    wait_all_nodes_healthy,
)


@pytest.mark.hardware
def test_estop_reset_rearm_full_sequence(physical_bench):
    bench = physical_bench
    capture = bench.capture
    host = bench.restbus.host

    capture.clear()
    wait_all_nodes_healthy(bench)
    # Preconditions: ESTOP already clear on the bench (fresh power-on state).
    operator_start_reset(bench)

    # Enter AUTO and command traction.
    bench.io.mode_short_press()
    host.set_drive(1500, gear=1, active=True)
    host.start()
    try:
        capture.wait_for(
            lambda s: s.bus == LOW and s.key == RT_DRIVE_CMD and s.is_valid
            and s.values["motor_speed_mmps"] > 0,
            timeout=5.0,
        )
        # ── PRESS physical ESTOP ────────────────────────────────────────
        bench.io.estop_press()
        try:
            capture.wait_for(
                lambda s: s.bus == LOW and s.key == SYS_SAFETY_STS and s.is_valid
                and s.values["estop_active"] == 1,
                timeout=3.0,
            )
            # 0x001 present on Low, 0x113=OFF, RT zeroes 0x204, MTR shows estop.
            assert any(
                s.bus == LOW and s.key == "safety:safety_estop" for s in capture.all()
            ), "no 0x001 SAFETY_ESTOP on Low during ESTOP"
            assert any(
                s.bus == LOW and s.key == SYS_PWR_CMD and s.is_valid
                and s.values["power_state"] == 0
                for s in capture.all()
            ), "SYS never published 0x113=OFF during ESTOP"
            wait_for_valid(capture, LOW, MTR_NODE_STATUS, timeout=3.0)
            assert safety.mtr_estop_active(capture) or safety.mtr_estop_latched(capture), \
                "MTR did not enter ESTOP (0x502 estop flags)"
        finally:
            bench.io.estop_release()
            estop_end = time.perf_counter()

        # ── RELEASE ESTOP, keep AUTO + drive: MUST NEVER resume ─────────
        host.set_drive(1500, gear=1, active=True)
        try:
            with never_window(capture, lambda: safety.motion_unsafe(capture)):
                while time.perf_counter() < estop_end + 10.0:
                    time.sleep(0.25)
        finally:
            pass

        # ── PRESS START -> operator reset ───────────────────────────────
        clear_before = count_valid_clear_frames(bench)
        operator_start_reset(bench)
        assert count_valid_clear_frames(bench) >= clear_before + 2, \
            "SYS did not publish two advancing E2E-valid 0x011=0 frames after START"

        # MTR clears its latch but traction must stay disabled (no fresh rearm).
        assert not safety.mtr_estop_latched(capture), "MTR latch did not clear"

        with never_window(capture, lambda: safety.motor_unsafe(capture)):
            host.set_drive(1500, gear=1, active=True)
            time.sleep(2.0)  # keep commanding; MTR must refuse without fresh 0x113 OFF->ON

        # ── explicit power OFF->ON rearm, then fresh AUTO + drive ───────
        bench.power.cycle("all", off_ms=3000)
        operator_start_reset(bench)
        bench.io.mode_short_press()
        capture.clear()
        host.set_drive(1500, gear=1, active=True)
        capture.wait_for(
            lambda s: s.bus == LOW and s.key == MTR_NODE_STATUS and s.is_valid
            and s.values["output_enabled"] == 1,
            timeout=10.0,
        )
    finally:
        host.set_drive(0, active=False)
        host.stop()


@pytest.mark.hardware
def test_estop_release_without_reset_never_drives(physical_bench):
    """ESTOP press + release WITHOUT START: software latch must stay set."""
    bench = physical_bench
    capture = bench.capture
    host = bench.restbus.host
    operator_start_reset(bench)
    host.set_drive(1500, gear=1, active=True)
    host.start()
    try:
        bench.io.estop_press()
        time.sleep(0.3)
        bench.io.estop_release()
        time.sleep(0.3)
        assert safety.sys_estop_active(capture), "SYS cleared without START reset"
        with never_window(capture, lambda: safety.motion_unsafe(capture)):
            time.sleep(3.0)
    finally:
        host.set_drive(0, active=False)
        host.stop()


# ── harness self-tests (no hardware) ────────────────────────────────────
@pytest.mark.selftest
def test_selftest_sys_safety_e2e_clear_frame_detection(vb):
    """The 'two advancing valid clear frames' logic keys off E2E CRC + counter."""

    def make_estop(estop: int, counter: int) -> bytes:
        payload = bytearray(5)
        payload[0] = estop
        payload[1] = 1
        payload[3] = counter
        payload[4] = sys_safety_sts_crc(bytes(payload[:4]))
        return bytes(payload)

    good1 = make_estop(0, 7)
    good2 = make_estop(0, 8)
    bad_crc = bytearray(good1)
    bad_crc[4] ^= 0xFF
    assert safety_sts_e2e_ok(good1)
    assert safety_sts_e2e_ok(good2)
    assert not safety_sts_e2e_ok(bytes(bad_crc))


@pytest.mark.selftest
def test_selftest_0x001_and_0x113_wire_encode(vb):
    """Bench can express the frames the ESTOP scenario asserts on."""
    from bench.wire import decode_message

    pwr_off = encode_message("low", 0x113, {"power_state": 0, "rolling_counter": 0})
    key, values = decode_message("low", 0x113, pwr_off)
    assert key == SYS_PWR_CMD and values["power_state"] == 0
    payload = bytearray(5)
    payload[0] = 1
    payload[1] = 1
    payload[4] = sys_safety_sts_crc(bytes(payload[:4]))
    _, estop_values = decode_message("low", 0x011, bytes(payload))
    assert estop_values["estop_active"] == 1
