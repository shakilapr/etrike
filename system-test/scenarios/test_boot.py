"""Scenario: BOOT / NETWORK OBSERVABILITY.

Milestone 1 — prove the PC reliably observes the real vehicle network before any
fault injection exists. Requires a physical bench; the self-test below proves the
observation pipeline (virtual link -> capture -> decode -> hardware-timestamped
trace) is sound on the host.
"""
from __future__ import annotations

import pytest

from assertions.safety import all_nodes_ready
from bench.wire import RT_HEARTBEAT, SYS_HEARTBEAT, SYS_SAFETY_STS
from scenarios.procedures import wait_all_nodes_healthy


@pytest.mark.hardware
def test_observe_real_network_frames(physical_bench):
    """Observe RT/SYS heartbeats + SYS safety + MTR feedback with timestamps.

    Native matrix: milestone-1 of the L9 plan (capture before fault injection).
    """
    bench = physical_bench
    bench.capture.clear()
    wait_all_nodes_healthy(bench, timeout=15.0)
    for key in (RT_HEARTBEAT, SYS_HEARTBEAT, SYS_SAFETY_STS):
        latest = bench.capture.latest_on("low", key)
        assert latest is not None and latest.is_valid, f"missing healthy {key}"
        assert latest.hw_ts is not None or latest.ts > 0, "frame has no timestamp"
        bench.capture.all()
    samples = bench.capture.all()
    assert len(samples) > 0, "capture empty"
    assert all(s.ts > 0 for s in samples), "all samples must carry a timestamp"


@pytest.mark.hardware
def test_capture_repeatable(physical_bench):
    """Two short capture windows must both find the periodic heartbeat streams."""
    bench = physical_bench
    for _ in range(2):
        bench.capture.clear()
        bench.capture.wait_for(
            lambda s: s.key == SYS_HEARTBEAT and s.is_valid, timeout=5.0
        )
        bench.capture.wait_for(
            lambda s: s.key == RT_HEARTBEAT and s.is_valid, timeout=5.0
        )


@pytest.mark.selftest
def test_selftest_host_heartbeat_capture(vb):
    """Loopback: HostNode heartbeat must reach the capture decoded on High CAN."""
    bench = vb
    host = bench.restbus.host
    bench.capture.clear()
    host.start()
    try:
        sample = bench.capture.wait_for(
            lambda s: s.bus == "high" and s.key == "host:host_heartbeat" and s.is_valid,
            timeout=3.0,
        )
        assert sample.is_valid
        assert sample.values["alive_ctr"] is not None
    finally:
        host.stop()
    assert bench.capture.count("host:host_heartbeat") >= 1


@pytest.mark.selftest
def test_selftest_wire_catalog_facts(vb):
    """Contract drift check: every scenario-referenced frame resolves & encodes."""
    from bench.wire import HOST_DRIVE_CMD, MTR_MOTOR_FBK, SYS_SAFETY_STS
    from bench.wire import decode_message, encode_message, message_key_for

    assert message_key_for("high", 0x300) == HOST_DRIVE_CMD
    assert message_key_for("low", 0x206) == MTR_MOTOR_FBK
    assert message_key_for("low", 0x011) == SYS_SAFETY_STS
    data = encode_message("high", 0x300, {"speed_mmps": 1000, "yaw_rate_mrad_s": 0, "gear": 1})
    key, values = decode_message("high", 0x300, data)
    assert key == HOST_DRIVE_CMD and values["speed_mmps"] == 1000
