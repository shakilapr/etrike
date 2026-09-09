"""Scenario: TRACTION CHAIN (boot -> AUTO -> drive -> Host-loss watchdog).

Milestone 2 — the production traction chain through the real ECUs:
    PC Host 0x300 -> REAL RT 0x204 -> REAL MTR 0x206
and the watchdog behaviour when the Host drive stream stops.

Hardware tests are gated on a physical bench; the self-tests exercise the Host
restbus emission and stop-on-inactive contract on the loopback link.
"""
from __future__ import annotations

import time

import pytest

from assertions import safety
from assertions.temporal import always_window, never_window, wait_for_valid
from bench.clock import wait_until
from bench.wire import LOW, MTR_MOTOR_FBK, RT_DRIVE_CMD, SYS_SAFETY_STS
from scenarios.procedures import wait_sys_clear


def _rt_drive_gone_or_zero(bench, staleness_s: float = 0.8) -> bool:
    latest = bench.capture.latest_on(LOW, RT_DRIVE_CMD)
    if latest is None:
        return True
    if latest.is_valid and latest.values["motor_speed_mmps"] == 0:
        return True
    return (time.perf_counter() - latest.ts) > staleness_s


@pytest.mark.hardware
def test_traction_chain_1000(physical_bench):
    """Boot, AUTO, drive 1000 mm/s; observe RT 0x204 and MTR acceptance.

    Native matrix: covers RT/MTR authority hand-off on the real bus.
    """
    bench = physical_bench
    capture = bench.capture
    host = bench.restbus.host
    wait_sys_clear(bench)

    bench.io.mode_short_press()          # MANUAL -> AUTO (physical SYS input)
    host.set_drive(1000, gear=1, active=True)
    host.start()
    try:
        rt = capture.wait_for(
            lambda s: s.bus == LOW and s.key == RT_DRIVE_CMD and s.is_valid
            and abs(s.values["motor_speed_mmps"] - 1000) <= 100,
            timeout=5.0,
        )
        assert rt.is_valid, "RT 0x204 never echoed ~1000 mm/s"
        mtr = capture.wait_for(
            lambda s: s.bus == LOW and s.key == MTR_MOTOR_FBK and s.is_valid
            and abs(s.values["motor_command_speed_mmps"] - 1000) <= 100,
            timeout=5.0,
        )
        assert mtr.is_valid, "MTR never accepted ~1000 mm/s command"
        assert safety.sys_estop_active(capture) is False
    finally:
        host.set_drive(0, active=False)
        host.stop()


@pytest.mark.hardware
def test_host_loss_watchdog_output_safe(physical_bench):
    """Stop Host command mid-drive; RT must zero 0x204 within its guard.

    MUST-NEVER property: no ESTOP latch and no 0x204 while stream is gone.
    """
    bench = physical_bench
    capture = bench.capture
    host = bench.restbus.host
    wait_sys_clear(bench)
    host.set_drive(1500, gear=1, active=True)
    host.start()
    try:
        capture.wait_for(
            lambda s: s.bus == LOW and s.key == RT_DRIVE_CMD and s.is_valid
            and s.values["motor_speed_mmps"] > 0,
            timeout=5.0,
        )
        host.set_drive(1500, active=False)   # stop transmitting 0x300/0x301
        with always_window(capture, lambda: safety.sys_estop_active(capture) is False):
            wait_until(lambda: _rt_drive_gone_or_zero(bench), timeout=3.0)
    finally:
        host.stop()


@pytest.mark.hardware
def test_rt_outputs_zero_when_sys_estop(physical_bench):
    """SYS ESTOP must force RT drive to zero the whole time it is latched."""
    bench = physical_bench
    capture = bench.capture
    host = bench.restbus.host
    wait_sys_clear(bench)
    host.set_drive(2000, gear=1, active=True)
    host.start()
    try:
        capture.wait_for(
            lambda s: s.bus == LOW and s.key == RT_DRIVE_CMD and s.is_valid
            and s.values["motor_speed_mmps"] > 0,
            timeout=5.0,
        )
        bench.io.estop_press()
        try:
            capture.wait_for(
                lambda s: s.bus == LOW and s.key == SYS_SAFETY_STS and s.is_valid
                and s.values["estop_active"] == 1,
                timeout=3.0,
            )
            # RT first zeroes its drive output, THEN any reappearance is a violation.
            wait_until(lambda: _rt_drive_gone_or_zero(bench), timeout=3.0)
            with never_window(capture, lambda: safety.motion_unsafe(capture)):
                wait_for_valid(capture, LOW, SYS_SAFETY_STS, timeout=1.0)
        finally:
            bench.io.estop_release()
    finally:
        host.set_drive(0, active=False)
        host.stop()


@pytest.mark.selftest
def test_selftest_host_drive_emission_and_stop(vb):
    """Host 0x300 present while driving, absent after drive inactive."""
    bench = vb
    host = bench.restbus.host
    bench.capture.clear()
    host.set_drive(1000, gear=1, active=True)
    host.start()
    try:
        bench.capture.wait_for(
            lambda s: s.bus == "high" and s.key == "host:host_drive_cmd" and s.is_valid
            and s.values["speed_mmps"] == 1000,
            timeout=3.0,
        )
        sent_before = host.sent_count.get(0x300, 0)
        host.set_drive(1000, active=False)
        import time as _t
        _t.sleep(0.12)
        sent_after = host.sent_count.get(0x300, 0)
        assert sent_after == sent_before, "0x300 still transmitted after drive inactive"
    finally:
        host.stop()
