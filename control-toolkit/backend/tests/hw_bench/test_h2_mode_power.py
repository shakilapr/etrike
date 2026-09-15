"""H2 — power and mode authority gateway (Host -> RT -> Low -> SYS).

SYS is the sole mode authority: it publishes 0x110/0x113 on Low; RT mirrors the
authoritative mode into 0x210 and gates motion authority on it.
"""

from __future__ import annotations

import pytest

from harness import (
    CAN_RT_NODE_STATUS,
    CAN_RT_STATE_RPT,
    CAN_SYS_MODE_CMD,
    CAN_SYS_PWR_CMD,
    HIGH,
    LOW,
    MODE_AUTO,
    MODE_MANUAL,
)

pytestmark = pytest.mark.hw_bench


def test_power_on_gateway(bench):
    """hmi_pwr_req req_start=1 -> SYS 0x113 power_state=1 (contactor ON)."""
    ok, state = bench.command_power(True)
    assert ok, f"SYS 0x113 never reached power_state=1: {state.get((LOW, CAN_SYS_PWR_CMD))}"
    assert bench.signal(LOW, CAN_SYS_PWR_CMD, "power_state") == 1


def test_auto_mode_transition(bench):
    """3x hmi_mode_req req_mode=1 -> SYS 0x110 AUTO, RT 0x210 AUTO, output_enabled."""
    ok_power, _ = bench.command_power(True)
    assert ok_power, "power ON prerequisite failed"

    ok, state = bench.command_mode(True)
    assert ok, (
        "AUTO transition failed: SYS_MODE_CMD or RT_STATE_RPT never reported mode=1 "
        f"(SYS={state.get((LOW, CAN_SYS_MODE_CMD))}, RT={state.get((HIGH, CAN_RT_STATE_RPT))})"
    )
    assert bench.signal(LOW, CAN_SYS_MODE_CMD, "mode") == MODE_AUTO
    assert bench.signal(HIGH, CAN_RT_STATE_RPT, "mode") == MODE_AUTO

    ok, _ = bench.wait_signal(HIGH, CAN_RT_NODE_STATUS, "output_enabled", expected=1, timeout_s=3.0)
    assert ok, "RT 0x501 output_enabled never asserted in AUTO"


def test_manual_mode_takeover(bench):
    """hmi_mode_req req_mode=0 drops SYS and RT back to MANUAL."""
    ok_power, _ = bench.command_power(True)
    assert ok_power, "power ON prerequisite failed"
    ok_auto, _ = bench.command_mode(True)
    assert ok_auto, "could not reach AUTO before takeover test"

    ok, state = bench.command_mode(False)
    assert ok, f"did not return to MANUAL: {state.get((LOW, CAN_SYS_MODE_CMD))}"
    assert bench.signal(LOW, CAN_SYS_MODE_CMD, "mode") == MODE_MANUAL
    assert bench.signal(HIGH, CAN_RT_STATE_RPT, "mode") == MODE_MANUAL
