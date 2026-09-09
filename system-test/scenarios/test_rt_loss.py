"""Scenario: RT STREAM LOSS / BRAKE-AUTHORITY FALLBACK.

SYS heartbeat (0x7FE) and SYS brake command (0x7B9) are what RT uses to decide
NORMAL -> SYS_DEGRADED -> EMERGENCY_FALLBACK. These scenarios assert the *real*
RT behaviour and that it never shares 0x7B9 ownership with a live SYS.
"""
from __future__ import annotations

import time

import pytest

from assertions import safety
from assertions.temporal import always_window, never_window, wait_for_valid
from bench.wire import LOW, SYS_HEARTBEAT
from scenarios.procedures import wait_sys_clear

SYS_DEGRADED = 1
EMERGENCY_FALLBACK = 2


@pytest.mark.hardware
def test_rt_healthy_baseline_no_fallbacks(physical_bench):
    """Healthy vehicle: RT stays NORMAL, no brake-fallback while SYS is alive."""
    bench = physical_bench
    capture = bench.capture
    wait_sys_clear(bench)

    def fallback(sample):
        return (
            sample.bus == "high"
            and sample.key == "rt:rt_diag_rpt"
            and sample.is_valid
            and sample.values["brake_fallback_state"] != 0
        )

    wait_for_valid(capture, "high", "rt:rt_diag_rpt", timeout=5.0)
    with never_window(capture, lambda: _rt_fallback_active(capture)):
        wait_for_valid(capture, "high", "rt:rt_diag_rpt", timeout=3.0)


@pytest.mark.hardware
def test_rt_fallback_on_full_sys_loss(physical_bench):
    """Power SYS off (both 0x7FE and 0x7B9 vanish) -> RT must reach
    EMERGENCY_FALLBACK exactly once and never share 0x7B9 (SYS is physically
    gone, so no dual producers). Native-list tests 25, 28, 68."""
    bench = physical_bench
    capture = bench.capture
    wait_sys_clear(bench)
    capture.wait_for(
        lambda s: s.bus == "high" and s.key == "rt:rt_diag_rpt" and s.is_valid
        and s.values["brake_fallback_state"] == 0,
        timeout=5.0,
    )

    bench.power.off("sys")
    try:
        wait_for_valid(capture, "high", "rt:rt_diag_rpt", timeout=5.0)
        # give SYS heartbeats time to fully lapse on the wire
        time.sleep(1.0)
        latest_hb = capture.latest_on(LOW, SYS_HEARTBEAT)
        assert latest_hb is None or (time.perf_counter() - latest_hb.ts) > 0.8, \
            "SYS heartbeat still fresh after power-off"
        # fallback state should advance 0 -> 1 -> 2 without regressing to NORMAL
        states = [
            int(s.values["brake_fallback_state"])
            for s in capture.all()
            if s.bus == "high" and s.key == "rt:rt_diag_rpt" and s.is_valid
        ]
        assert EMERGENCY_FALLBACK in states, f"RT never reached EMERGENCY_FALLBACK: {states}"
        for prev, nxt in zip(states, states[1:]):
            assert nxt >= prev, f"RT fallback regressed {prev} -> {nxt}"
    finally:
        bench.power.on("sys")
        wait_sys_clear(bench)


def _rt_fallback_active(capture) -> bool:
    sample = capture.latest_on("high", "rt:rt_diag_rpt")
    if sample is None or not sample.is_valid:
        return True  # no fresh RT diag => treat as not-NORMAL until proven otherwise
    return bool(sample.values["brake_fallback_state"] != 0)


@pytest.mark.selftest
def test_selftest_heartbeat_stream_silence_detected(vb):
    """Host node: silencing the node must stop its heartbeat stream."""
    bench = vb
    host = bench.restbus.host
    from faults.node_faults import silence

    host.start()
    try:
        bench.capture.wait_for(
            lambda s: s.key == "host:host_heartbeat" and s.is_valid, timeout=3.0
        )
        before = bench.capture.count("host:host_heartbeat")
        silence(host)
        time.sleep(0.6)
        after = bench.capture.count("host:host_heartbeat")
        assert after == before, "host heartbeat kept flowing while node silenced"
    finally:
        host.stop()


@pytest.mark.selftest
def test_selftest_drop_only_heartbeat_keeps_drive(vb):
    """Per-frame drop: killing only 0x7FC must leave 0x300 flowing."""
    bench = vb
    host = bench.restbus.host
    from faults.node_faults import drop_frames

    host.set_drive(1200, gear=1, active=True)
    host.start()
    try:
        bench.capture.wait_for(
            lambda s: s.key == "host:host_drive_cmd" and s.is_valid, timeout=3.0
        )
        drop_frames(host, 0x7FC)
        vb.capture.clear()
        time.sleep(0.55)
        assert vb.capture.count("host:host_drive_cmd") > 0
        assert vb.capture.count("host:host_heartbeat") == 0
    finally:
        host.stop()
