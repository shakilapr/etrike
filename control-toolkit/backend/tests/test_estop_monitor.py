"""Durable ESTOP diagnostic/audit event capture."""

from __future__ import annotations

import time

from control_toolkit.models.frames import (
    ChannelId,
    Direction,
    FrameSource,
    RawFrameEnvelope,
)
from control_toolkit.models.state import FreshnessState, MessageState, SignalValue
from control_toolkit.services.diagnostics import DiagnosticsService
from control_toolkit.services.estop_monitor import EstopEventMonitor


def _frame(bus: ChannelId, can_id: int, dlc: int = 0) -> RawFrameEnvelope:
    return RawFrameEnvelope(
        adapter_epoch=1,
        channel=bus,
        backend_arrival_ns=time.monotonic_ns(),
        can_id=can_id,
        dlc=dlc,
        data=b"\x00" * dlc,
        channel_sequence=1,
        direction=Direction.RX,
        source=FrameSource.PHYSICAL,
    )


def _state(name: str, bus: str, can_id: int, signals: dict[str, int]) -> MessageState:
    return MessageState(
        bus=bus,
        can_id=can_id,
        name=name,
        freshness=FreshnessState.LIVE,
        signals={
            key: SignalValue(engineering_value=value, valid=True)
            for key, value in signals.items()
        },
    )


def test_external_estop_frame_records_unknown_origin():
    diagnostics = DiagnosticsService()
    monitor = EstopEventMonitor(diagnostics)

    monitor.observe(
        _state("SAFETY_ESTOP", "high", 0x001, {}),
        _frame(ChannelId.HIGH, 0x001),
    )

    event = diagnostics.list_events(limit=1)[0]
    assert event["code"] == "safety.estop_frame"
    assert event["bus"] == "high"
    assert event["evidence"]["origin"] == "unknown"
    assert "sender=Any and DLC=0" in event["detail"]


def test_host_inject_is_correlated_and_rt_reason_is_deduplicated():
    diagnostics = DiagnosticsService()
    monitor = EstopEventMonitor(diagnostics)
    monitor.note_host_inject(source="ui:header")

    monitor.observe(
        _state("SAFETY_ESTOP", "low", 0x001, {}),
        _frame(ChannelId.LOW, 0x001),
    )
    rt_state = _state(
        "RT_STATE_RPT",
        "high",
        0x210,
        {"mode": 2, "safety_state": 1, "estop_reason": 5},
    )
    monitor.observe(rt_state, _frame(ChannelId.HIGH, 0x210, dlc=6))
    monitor.observe(rt_state, _frame(ChannelId.HIGH, 0x210, dlc=6))

    events = diagnostics.list_events(limit=10)
    rt_events = [event for event in events if event["code"] == "safety.rt_estop"]
    frame_event = next(event for event in events if event["code"] == "safety.estop_frame")
    assert len(rt_events) == 1
    assert rt_events[0]["title"] == "RT safety stop · CAN ESTOP received"
    assert rt_events[0]["evidence"]["reason_code"] == 5
    assert rt_events[0]["evidence"]["host_correlated"] is True
    assert frame_event["evidence"]["origin"] == "host_toolkit"


def test_one_clear_bus_does_not_clear_other_active_rt_report():
    diagnostics = DiagnosticsService()
    monitor = EstopEventMonitor(diagnostics)
    monitor.observe(
        _state(
            "RT_STATE_RPT",
            "high",
            0x210,
            {"mode": 2, "safety_state": 1, "estop_reason": 5},
        ),
        _frame(ChannelId.HIGH, 0x210, dlc=6),
    )
    monitor.observe(
        _state(
            "RT_STATE_RPT",
            "low",
            0x210,
            {"mode": 0, "safety_state": 0, "estop_reason": 0},
        ),
        _frame(ChannelId.LOW, 0x210, dlc=6),
    )

    assert not any(
        event["code"] == "safety.rt_estop_cleared"
        for event in diagnostics.list_events(limit=10)
    )
