"""Scenario: BUS / NODE RECOVERY AFTER ESTOP AND POWER CYCLES.

Deterministic repeated reset (native test 2), single-ECU power-cycle startup
(native tests 78-80) and physical-ESTOP-held-during-boot (native test 80-81).
"""
from __future__ import annotations

import random
import time

import pytest

from assertions import safety
from assertions.temporal import never_window, wait_for_valid
from bench.wire import LOW, MTR_NODE_STATUS, SYS_SAFETY_STS
from scenarios.procedures import operator_start_reset, wait_all_nodes_healthy, wait_sys_clear


@pytest.mark.hardware
def test_repeated_reset_loop_is_deterministic(physical_bench):
    """Repeat ESTOP -> release -> reset many times with varied timing offsets.

    The SYS 0x011 clear rate and RT's rate-limited 0x001 handling must never
    create a timing-dependent reset (native test 2, ESTOP-reset-loop family).
    """
    bench = physical_bench
    capture = bench.capture
    random.seed(0xC0FFEE)
    wait_sys_clear(bench)

    for iteration in range(6):
        bench.io.estop_press()
        time.sleep(0.2 + random.uniform(0.0, 0.4))
        bench.io.estop_release()
        time.sleep(0.05 + random.uniform(0.0, 0.9))  # timing offset before START
        latest = capture.latest_on(LOW, SYS_SAFETY_STS)
        assert latest is not None and latest.is_valid and latest.values["estop_active"] == 1, \
            f"iteration {iteration}: SYS cleared before operator START"
        operator_start_reset(bench)
        wait_sys_clear(bench)
        time.sleep(0.4)
        latest = capture.latest_on(LOW, SYS_SAFETY_STS)
        assert latest is not None and latest.is_valid and latest.values["estop_active"] == 0, \
            f"iteration {iteration}: reset did not stick"


@pytest.mark.hardware
def test_sys_power_cycle_alone_does_not_clear_consumers(physical_bench):
    """ESTOP, power-cycle SYS only: RT/MTR must stay latched; SYS must restart
    into ESTOP rather than publishing clear frames (native test 78)."""
    bench = physical_bench
    capture = bench.capture
    bench.io.estop_press()
    time.sleep(0.3)
    bench.io.estop_release()

    bench.power.cycle("sys", off_ms=2500)
    time.sleep(1.0)
    wait_for_valid(capture, LOW, SYS_SAFETY_STS, timeout=10.0)
    latest = capture.latest_on(LOW, SYS_SAFETY_STS)
    assert latest is not None and latest.is_valid, "SYS did not restart publishing 0x011"
    assert latest.values["estop_active"] == 1, "SYS restarted clear while RT/MTR still latched"
    with never_window(capture, lambda: safety.motor_unsafe(capture)):
        time.sleep(2.0)
    operator_start_reset(bench)
    wait_sys_clear(bench)


@pytest.mark.hardware
def test_physical_estop_held_during_boot(physical_bench):
    """Boot all ECUs while the physical ESTOP is already pressed. No actuator may
    energise and the software latch must hold until a proper reset (native 80)."""
    bench = physical_bench
    capture = bench.capture
    bench.io.estop_press()
    try:
        bench.power.cycle("all", off_ms=3000)
        wait_all_nodes_healthy(bench, timeout=15.0)
        time.sleep(1.0)
        with never_window(capture, lambda: safety.motor_unsafe(capture)):
            time.sleep(2.0)
        latest = capture.latest_on(LOW, SYS_SAFETY_STS)
        assert latest is not None and latest.is_valid and latest.values["estop_active"] == 1
    finally:
        bench.io.estop_release()
    operator_start_reset(bench)
    wait_sys_clear(bench)


@pytest.mark.selftest
def test_selftest_always_window_flags_miss(vb):
    """Required condition going false inside an always-window must trip."""
    from assertions.temporal import Violation, always_window

    ok = {"on": True}

    def required():
        return ok["on"]

    with pytest.raises(Violation):
        with always_window(vb.capture, required, sample_ms=0.01):
            time.sleep(0.03)
            ok["on"] = False
            time.sleep(0.05)


@pytest.mark.selftest
def test_selftest_always_window_clean(vb):
    from assertions.temporal import always_window

    with always_window(vb.capture, lambda: True, sample_ms=0.01):
        time.sleep(0.05)


@pytest.mark.selftest
def test_selftest_node_reboot_helper_restarts(vb):
    from faults.node_faults import reboot

    host = vb.restbus.host
    host.start()
    try:
        vb.capture.clear()
        vb.capture.wait_for(
            lambda s: s.key == "host:host_heartbeat" and s.is_valid, timeout=3.0
        )
        before = host.sent_count.get(0x7FC, 0)
        reboot(host)
        vb.capture.clear()
        vb.capture.wait_for(
            lambda s: s.key == "host:host_heartbeat" and s.is_valid, timeout=3.0
        )
        assert host.sent_count.get(0x7FC, 0) > before, "host did not restart after reboot()"
    finally:
        host.stop()
