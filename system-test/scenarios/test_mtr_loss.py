"""Scenario: MTR FEEDBACK / AUTHORITY LOSS AND RECOVERY.

Real-node 0x206 / 0x502 dropout scenarios (native list 6-9, 54-58) need a
bus-mastering fault adapter and are gated accordingly. What the physical bench
can exercise today with power + restbus + GPIO: single-ECU power-cycles during
ESTOP (native list 76-79) proving no consumer silently resumes.
"""
from __future__ import annotations

import time

import pytest

from assertions import safety
from assertions.temporal import never_window, wait_for_valid
from bench.wire import LOW, MTR_NODE_STATUS
from scenarios.procedures import operator_start_reset, wait_sys_clear


@pytest.mark.hardware
def test_power_cycle_mtr_only_during_estop_stays_stopped(physical_bench):
    """ESTOP the whole system, power-cycle ONLY MTR: it must not resume motion
    while SYS still publishes estop_active=1 (native test 76)."""
    bench = physical_bench
    capture = bench.capture
    host = bench.restbus.host
    wait_sys_clear(bench)

    host.set_drive(1500, gear=1, active=True)
    host.start()
    try:
        bench.io.estop_press()
        try:
            capture.wait_for(
                lambda s: s.bus == LOW and s.key == MTR_NODE_STATUS and s.is_valid
                and (s.values["estop_active"] == 1 or s.values["estop_latched"] == 1),
                timeout=3.0,
            )
        finally:
            bench.io.estop_release()
        bench.power.cycle("mtr", off_ms=2000)
        time.sleep(0.5)
        # SYS still latched ESTOP -> motion MUST stay off after MTR reboot.
        with never_window(capture, lambda: safety.motor_unsafe(capture)):
            time.sleep(3.0)
    finally:
        host.set_drive(0, active=False)
        host.stop()


@pytest.mark.hardware
def test_power_cycle_rt_only_during_estop_stays_stopped(physical_bench):
    """ESTOP, power-cycle RT only: RT re-acquires the SYS latch and stays stopped
    (native test 77)."""
    bench = physical_bench
    capture = bench.capture
    host = bench.restbus.host
    wait_sys_clear(bench)

    bench.io.estop_press()
    time.sleep(0.4)
    bench.io.estop_release()
    bench.power.cycle("rt", off_ms=2000)
    time.sleep(0.5)
    with never_window(capture, lambda: safety.motion_unsafe(capture)):
        time.sleep(3.0)
    # Operator reset must still work after the RT reboot.
    operator_start_reset(bench)
    assert safety.sys_estop_active(capture) is False


@pytest.mark.selftest
def test_selftest_0x502_decode_roundtrip(vb):
    """Bench decoder reads MTR node status flags used by safety predicates."""
    from bench.wire import decode_message, encode_message

    payload = encode_message(
        "low", 0x502,
        {
            "node_state": 5,
            "block_mask": 0,
            "estop_active": 1,
            "ready": 0,
            "command_received": 0,
            "command_nonzero": 0,
            "output_enabled": 0,
            "estop_latched": 1,
            "recovery_pending": 0,
            "degraded": 0,
            "rolling_counter": 0,
            "e2e_crc": 0,
        },
    )
    key, values = decode_message("low", 0x502, payload)
    assert key == "mtr:mtr_node_status"
    assert values["estop_latched"] == 1 and values["node_state"] == 5


@pytest.mark.selftest
def test_selftest_never_window_detects_transient_violation(vb):
    """A one-frame traction blip inside a MUST-NEVER window must trip the watch."""
    from assertions.temporal import Violation, never_window

    danger = {"on": False}

    def danger_fn():
        return danger["on"]

    with pytest.raises(Violation):
        with never_window(vb.capture, danger_fn, sample_ms=0.01):
            time.sleep(0.03)
            danger["on"] = True
            time.sleep(0.05)
            danger["on"] = False
            time.sleep(0.05)
