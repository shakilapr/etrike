"""H1 — link liveness, telemetry rates, node state, and RT gateway baseline.

Observes what the two controllers broadcast with no host stimulus beyond the
bench session. These are the "both boards booted cleanly" invariants from the
hardware-bench handoff.
"""

from __future__ import annotations

import pytest

from harness import (
    CAN_RT_DRIVE_CMD,
    CAN_RT_HEARTBEAT,
    CAN_RT_MOTION_RPT,
    CAN_RT_NODE_STATUS,
    CAN_SYS_DIAG_RPT,
    CAN_SYS_HEARTBEAT,
    CAN_SYS_NODE_STATUS,
    CAN_SYS_SAFETY_STS,
    HIGH,
    LOW,
    NODE_STATES_OPERATIONAL,
    signal_of,
)

pytestmark = pytest.mark.hw_bench


def test_rt_heartbeat_on_high_managed_bus(bench):
    """RT 0x7FD heartbeat is live on the High bus (nominal 2 Hz)."""
    ok, _ = bench.wait_rate(HIGH, CAN_RT_HEARTBEAT, min_hz=1.0, timeout_s=6.0)
    assert ok, "RT 0x7FD heartbeat never became live at >=1 Hz on High"


def test_sys_heartbeat_on_low_managed_bus(bench):
    """SYS 0x7FE heartbeat is live on the Low bus (nominal 10 Hz)."""
    ok, _ = bench.wait_rate(LOW, CAN_SYS_HEARTBEAT, min_hz=5.0, timeout_s=6.0)
    assert ok, "SYS 0x7FE heartbeat never became live at >=5 Hz on Low"


def test_sys_safety_status_clean(bench):
    """SYS 0x011 reports ESTOP clear with a healthy heartbeat on both buses."""
    # SYS boots into ESTOP and needs a staged reset; normalise, then wait for
    # the clear to actually be broadcast before asserting.
    bench.ensure_operational()
    ok, state = bench.wait_for(
        lambda s: signal_of(s.get((LOW, CAN_SYS_SAFETY_STS)), "estop_active") == 0,
        timeout_s=6.0,
    )
    assert ok, f"SYS 0x011 never reported estop_active=0: {state.get((LOW, CAN_SYS_SAFETY_STS))}"
    msg = state[(LOW, CAN_SYS_SAFETY_STS)]
    assert signal_of(msg, "heartbeat_ok") == 1, f"SYS 0x011 heartbeat_ok != 1: {msg}"

    ok, state = bench.wait_live(HIGH, CAN_SYS_SAFETY_STS, timeout_s=4.0)
    assert ok, "SYS 0x011 was never forwarded Low->High by RT"
    assert signal_of(state[(HIGH, CAN_SYS_SAFETY_STS)], "estop_active") == 0


def test_rt_drive_cmd_idle_stream(bench):
    """RT 0x204 streams at the 100 Hz contract with a zero idle setpoint."""
    ok, _ = bench.wait_rate(LOW, CAN_RT_DRIVE_CMD, min_hz=50.0, timeout_s=6.0)
    assert ok, "RT 0x204 not streaming at >=50 Hz on Low"
    value = bench.signal(LOW, CAN_RT_DRIVE_CMD, "motor_speed_mmps")
    assert value == 0, f"RT 0x204 idle setpoint != 0 ({value})"


def test_rt_motion_report_on_high(bench):
    """RT 0x121 motion report streams on the High bus (~100 Hz)."""
    ok, _ = bench.wait_rate(HIGH, CAN_RT_MOTION_RPT, min_hz=50.0, timeout_s=6.0)
    assert ok, "RT 0x121 motion report not streaming at >=50 Hz on High"


def test_node_status_standby_and_healthy(bench):
    """Both node-status reports are operational with no block mask and ready."""
    ok, state = bench.wait_live(LOW, CAN_SYS_NODE_STATUS, timeout_s=4.0)
    assert ok, "SYS 0x500 not live on Low"
    ok, state = bench.wait_live(LOW, CAN_RT_NODE_STATUS, timeout_s=4.0)
    assert ok, "RT 0x501 not live on Low"

    def _healthy(s) -> bool:
        for bus, cid in ((LOW, CAN_SYS_NODE_STATUS), (LOW, CAN_RT_NODE_STATUS)):
            msg = s.get((bus, cid))
            if not msg:
                return False
            if signal_of(msg, "node_state") not in NODE_STATES_OPERATIONAL:
                return False
            if signal_of(msg, "block_mask") != 0:
                return False
            if signal_of(msg, "ready") != 1:
                return False
            if signal_of(msg, "estop_active") != 0:
                return False
        return True

    ok, state = bench.wait_for(_healthy, timeout_s=6.0)
    assert ok, (
        "node status not healthy (STANDBY/ACTIVE, ready=1, block_mask=0): "
        f"RT={state.get((LOW, CAN_RT_NODE_STATUS))}, SYS={state.get((LOW, CAN_SYS_NODE_STATUS))}"
    )


def test_rt_forwards_sys_reports_low_to_high(bench):
    """RT transparently forwards SYS safety, node status and diag Low->High."""
    for cid, label in (
        (CAN_SYS_SAFETY_STS, "SYS_SAFETY_STS 0x011"),
        (CAN_SYS_NODE_STATUS, "SYS_NODE_STATUS 0x500"),
        (CAN_SYS_DIAG_RPT, "SYS_DIAG_RPT 0x600"),
    ):
        ok, _ = bench.wait_live(HIGH, cid, timeout_s=6.0)
        assert ok, f"{label} was not forwarded Low->High by RT"
