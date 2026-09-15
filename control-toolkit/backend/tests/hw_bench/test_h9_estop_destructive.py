"""H9 — DESTRUCTIVE ESTOP trip verification (opt-in).

A latched SYS ESTOP on this actuator-less bench is a **one-way trip** for the
test host:

* Remote reset via ``hmi:host_estop_reset_req`` is rejected while MTR is absent:
  SYS answers 0x115 SYS_ESTOP_RESET_RSP with ``blocker_mask`` bit
  ``kResetBlockMtrEstopActive`` (0x0010) because MTR never acknowledges the
  ESTOP (sys-esp32/src/inhibit_state.h:120, called with
  ``g_mtr_ack_watchdog.has_acknowledged()``).
* Backend ``/control/estop/rearm`` only *emulates* SYS for SYS-less rigs; it
  cannot clear the real SYS, which keeps broadcasting 0x011 estop_active=1.

Recovery therefore requires a physical SYS reset (RST/EN button or power-cycle).

For that reason this module is skipped unless ``CTK_HW_ESTOP=1`` is set, and its
file name sorts after the reversible suites so it runs last. Run it explicitly:

    CTK_HW_ESTOP=1 pytest tests/hw_bench/test_h9_estop_destructive.py -v
"""

from __future__ import annotations

import os
import time

import pytest

from harness import (
    CAN_ESTOP_RESET_RSP,
    CAN_RT_DRIVE_CMD,
    CAN_RT_STATE_RPT,
    CAN_SEB_REQ,
    CAN_SYS_SAFETY_STS,
    ESTOP_RESET_TOKEN,
    GEAR_D,
    GEAR_N,
    HIGH,
    KEY_ESTOP_RESET_REQ,
    LOW,
    MODE_ESTOP,
    SEB_STROKE_RAW_ESTOP_MAX,
    signal_of,
)

pytestmark = [
    pytest.mark.hw_bench,
    pytest.mark.skipif(
        os.environ.get("CTK_HW_ESTOP") != "1",
        reason=(
            "destructive ESTOP trip: set CTK_HW_ESTOP=1 to run; "
            "a physical SYS reset (RST/EN or power-cycle) is required afterwards"
        ),
    ),
]

RESET_BLOCK_MTR_ESTOP_ACTIVE = 0x0010


def _reset_result(state):
    msg = state.get((LOW, CAN_ESTOP_RESET_RSP)) or state.get((HIGH, CAN_ESTOP_RESET_RSP))
    return signal_of(msg, "result"), signal_of(msg, "blocker_mask")


def test_estop_trip_safe_outputs_and_reset_handling(auto_ready):
    """0x001 on High -> ESTOP everywhere, safe outputs, and reset handling.

    The High-bus 0x001 is forwarded to Low by RT, so both SYS and RT latch the
    same trip. Safe outputs and the remote-reset reply are then verified.
    """
    bench = auto_ready
    bench.start_drive(1000, gear=GEAR_D)
    ok, _ = bench.wait_for(
        lambda s: abs((signal_of(s.get((LOW, CAN_RT_DRIVE_CMD)), "motor_speed_mmps") or 0) - 1000) <= 100,
        timeout_s=4.0,
    )
    assert ok, "drive never engaged before the ESTOP trip"

    assert bench.assert_estop(HIGH), "raw 0x001 injection failed on High"
    ok, state = bench.wait_for(
        lambda s: signal_of(s.get((LOW, CAN_SYS_SAFETY_STS)), "estop_active") == 1,
        timeout_s=4.0,
    )
    assert ok, "SYS 0x011 never reported estop_active=1 after 0x001"

    # RT must drop the drive command to zero / neutral.
    ok, state = bench.wait_for(
        lambda s: signal_of(s.get((LOW, CAN_RT_DRIVE_CMD)), "motor_speed_mmps") == 0
        and signal_of(s.get((LOW, CAN_RT_DRIVE_CMD)), "gear") == GEAR_N,
        timeout_s=3.0,
    )
    assert ok, f"RT 0x204 not zeroed/neutral in ESTOP: {state.get((LOW, CAN_RT_DRIVE_CMD))}"

    # SYS must command the SEB to maximum stroke.
    ok, state = bench.wait_signal(
        LOW, CAN_SEB_REQ, "stroke_request_raw", expected=SEB_STROKE_RAW_ESTOP_MAX, timeout_s=3.0
    )
    assert ok, (
        f"SYS 0x7B9 ESTOP stroke != {SEB_STROKE_RAW_ESTOP_MAX}: {state.get((LOW, CAN_SEB_REQ))}"
    )

    # RT must report ESTOP mode on 0x210.
    ok, state = bench.wait_signal(HIGH, CAN_RT_STATE_RPT, "mode", expected=MODE_ESTOP, timeout_s=4.0)
    assert ok, f"RT 0x210 mode != ESTOP: {state.get((HIGH, CAN_RT_STATE_RPT))}"

    # Host reset request: on this bench it must be either accepted (if a real
    # MTR later provides the ACK) or safely rejected while MTR is absent.
    for seq in (1, 2, 3):
        bench.inject(HIGH, KEY_ESTOP_RESET_REQ, {"request_seq": seq, "reset_token": ESTOP_RESET_TOKEN})
        time.sleep(0.03)

    ok, state = bench.wait_for(lambda s: _reset_result(s)[0] is not None, timeout_s=4.0)
    assert ok, "SYS 0x115 SYS_ESTOP_RESET_RSP never arrived"
    result, blockers = _reset_result(state)
    if result == 0:
        ok, _ = bench.wait_signal(
            LOW, CAN_SYS_SAFETY_STS, "estop_active", expected=0, timeout_s=6.0
        )
        assert ok, "reset response accepted but SYS 0x011 never cleared"
    else:
        assert blockers is not None, "0x115 reported REJECTED without a blocker_mask"
        assert blockers & RESET_BLOCK_MTR_ESTOP_ACTIVE, (
            "expected reset to be blocked by MTR-ESTOP-ACK while MTR is absent "
            f"(blocker_mask=0x{blockers:04x})"
        )
