"""H10 — node health, telemetry, and diagnostic counter soaks.

The scenario suites prove the *commands* are right. These tests prove the
**transport and node health** stay right while they are produced: heartbeats
stay live, node states stay operational, the MCP2515 stays out of bus-off with
no unexpected recovery, and rolling counters keep advancing.

The MCP2515 watchdog introduced for F1 must not false-fire: ``0x620``
``mcp_recovery_attempts`` and ``spi_fault_delta`` must stay flat on a healthy
bench.
"""

from __future__ import annotations

import time

import pytest

from harness import (
    CAN_RT_DIAG_RPT,
    CAN_RT_HEARTBEAT,
    CAN_RT_NODE_STATUS,
    CAN_RT_STATE_RPT,
    CAN_SYS_HEARTBEAT,
    CAN_SYS_NODE_STATUS,
    GEAR_D,
    HIGH,
    LOW,
    NODE_STATES_OPERATIONAL,
    is_live,
    signal_of,
)
from scenario import dur, hold, speed_is, track

pytestmark = [pytest.mark.hw_bench, pytest.mark.slow]


def test_node_health_soak(auto_ready):
    """Both nodes stay live/operational with no ESTOP over a soak window."""
    bench = auto_ready
    bench.start_drive(1000, gear=GEAR_D)
    track(bench, speed_is(1000, tol=150), "health: engage")

    deadline = time.monotonic() + dur(15)
    while time.monotonic() < deadline:
        state = bench.state_map()
        assert is_live(state.get((HIGH, CAN_RT_HEARTBEAT))), "RT heartbeat (High) not live"
        assert is_live(state.get((LOW, CAN_RT_HEARTBEAT))), "RT heartbeat (Low) not live"
        assert is_live(state.get((LOW, CAN_SYS_HEARTBEAT))), "SYS heartbeat not live"

        rt_state = signal_of(state.get((LOW, CAN_RT_STATE_RPT)), "mode")
        assert rt_state is not None, "RT state report missing"
        assert signal_of(state.get((LOW, CAN_RT_NODE_STATUS)), "estop_active") == 0
        assert signal_of(state.get((LOW, CAN_SYS_NODE_STATUS)), "estop_active") == 0
        assert signal_of(state.get((LOW, CAN_RT_NODE_STATUS)), "degraded") == 0
        time.sleep(0.5)

    bench.start_drive(0, gear=GEAR_D)


def test_mcp_diag_stable_no_false_recovery(auto_ready):
    """0x620 must show no bus-off, no recovery, and no SPI faults on a healthy bench."""
    bench = auto_ready
    bench.start_drive(800, gear=GEAR_D)

    ok, _ = bench.wait_for(lambda s: s.get((HIGH, CAN_RT_DIAG_RPT)) is not None, timeout_s=4.0)
    assert ok, "RT_DIAG_RPT 0x620 not present on High"

    first = bench.message(HIGH, CAN_RT_DIAG_RPT)
    attempts0 = signal_of(first, "mcp_recovery_attempts")
    assert attempts0 is not None, "0x620 missing mcp_recovery_attempts"

    deadline = time.monotonic() + dur(15)
    while time.monotonic() < deadline:
        msg = bench.message(HIGH, CAN_RT_DIAG_RPT)
        assert is_live(msg), "RT_DIAG_RPT 0x620 went stale (High bus down?)"
        assert signal_of(msg, "mcp_bus_off") == 0, f"unexpected MCP bus-off: {msg}"
        assert signal_of(msg, "mcp_recovering") == 0, f"unexpected MCP recovery: {msg}"
        assert signal_of(msg, "spi_fault_delta") == 0, f"SPI faults on a healthy bus: {msg}"
        assert signal_of(msg, "mcp_recovery_attempts") == attempts0, (
            f"F1 watchdog false-fired (recovery attempts changed): {msg}"
        )
        time.sleep(0.5)

    bench.start_drive(0, gear=GEAR_D)


def test_task_health_full_during_drive(auto_ready):
    """RT task_health reports every task alive for the whole maneuver."""
    bench = auto_ready
    bench.start_drive(1200, gear=GEAR_D)
    track(bench, speed_is(1200, tol=150), "task-health: engage")

    deadline = time.monotonic() + dur(10)
    while time.monotonic() < deadline:
        value = signal_of(bench.message(LOW, CAN_RT_STATE_RPT), "task_health")
        assert value is not None and (value & 0x0F) == 0x0F, (
            f"RT task_health lost a task: {value}"
        )
        time.sleep(0.5)

    bench.start_drive(0, gear=GEAR_D)


def test_heartbeat_counters_advance(bench):
    """RT and SYS heartbeat liveness counters keep incrementing."""
    bench.ensure_operational()

    def counter(bus: str, cid: int) -> int:
        return signal_of(bench.message(bus, cid), "alive_ctr")

    rt_first = counter(LOW, CAN_RT_HEARTBEAT)
    sys_first = counter(LOW, CAN_SYS_HEARTBEAT)
    assert rt_first is not None and sys_first is not None, "heartbeats missing"

    ok, _ = bench.wait_for(
        lambda s: signal_of(s.get((LOW, CAN_RT_HEARTBEAT)), "alive_ctr") not in (None, rt_first),
        timeout_s=3.0,
    )
    assert ok, "RT heartbeat alive_ctr did not advance"

    ok, _ = bench.wait_for(
        lambda s: signal_of(s.get((LOW, CAN_SYS_HEARTBEAT)), "alive_ctr") not in (None, sys_first),
        timeout_s=3.0,
    )
    assert ok, "SYS heartbeat alive_ctr did not advance"


def test_operational_node_states(bench):
    """Both node reports describe an operational (STANDBY/ACTIVE) state."""
    bench.ensure_operational()
    state = bench.state_map()

    rt = signal_of(state.get((LOW, CAN_RT_NODE_STATUS)), "node_state")
    sys = signal_of(state.get((LOW, CAN_SYS_NODE_STATUS)), "node_state")
    assert rt in NODE_STATES_OPERATIONAL, f"RT node_state not operational: {rt}"
    assert sys in NODE_STATES_OPERATIONAL, f"SYS node_state not operational: {sys}"
    hold(bench, "operational states", 1)
