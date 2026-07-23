"""Persist safety-stop observations as diagnostic and audit events.

The latest-state ESTOP report is intentionally a snapshot. This monitor records
the transition evidence that would otherwise disappear when an event frame
ages out of the live view.
"""

from __future__ import annotations

import time
from typing import Callable

from control_toolkit.models.frames import RawFrameEnvelope
from control_toolkit.models.state import MessageState
from control_toolkit.services.diagnostics import DiagnosticsService
from control_toolkit.services.estop_report import (
    RT_ESTOP_REASONS,
    reason_detail,
    reason_display,
)


def _signal_number(message: MessageState, key: str) -> int | None:
    signal = message.signals.get(key)
    if signal is None:
        return None
    value = signal.engineering_value
    try:
        return int(value)
    except (TypeError, ValueError):
        try:
            return int(float(value))
        except (TypeError, ValueError):
            return None


class EstopEventMonitor:
    """Turn raw 0x001 frames and RT reason transitions into durable evidence."""

    def __init__(
        self,
        diagnostics: DiagnosticsService,
        *,
        get_session_id: Callable[[], str | None] | None = None,
        host_correlation_window_s: float = 2.0,
    ) -> None:
        self._diagnostics = diagnostics
        self._get_session_id = get_session_id or (lambda: None)
        self._host_correlation_window_s = host_correlation_window_s
        self._host_inject_at: float | None = None
        self._host_inject_source: str | None = None
        self._rt_state_by_bus: dict[str, tuple[bool, int]] = {}
        self._last_rt_event: tuple[bool, int] | None = None
        self._last_rt_event_bus: str | None = None

    def note_host_inject(self, *, source: str) -> None:
        """Correlate the next observed 0x001 with an explicit toolkit action."""
        self._host_inject_at = time.monotonic()
        self._host_inject_source = source

    def reset(self) -> None:
        self._host_inject_at = None
        self._host_inject_source = None
        self._rt_state_by_bus.clear()
        self._last_rt_event = None
        self._last_rt_event_bus = None

    def observe(self, message: MessageState, frame: RawFrameEnvelope) -> None:
        if message.name == "SAFETY_ESTOP":
            self._observe_estop_frame(frame)
        elif message.name == "RT_STATE_RPT":
            self._observe_rt_state(message)

    def _host_correlation(self) -> tuple[bool, str | None]:
        if self._host_inject_at is None:
            return False, None
        recent = time.monotonic() - self._host_inject_at <= self._host_correlation_window_s
        return recent, self._host_inject_source if recent else None

    def _observe_estop_frame(self, frame: RawFrameEnvelope) -> None:
        correlated, source = self._host_correlation()
        bus = frame.channel.value
        if correlated:
            origin = "host_toolkit"
            origin_text = f"correlated with toolkit injection ({source or 'unknown UI source'})"
        else:
            origin = "unknown"
            origin_text = "origin unknown"
        detail = (
            f"SAFETY_ESTOP 0x001 received on {bus.title()}; {origin_text}. "
            "The protocol defines sender=Any and DLC=0, so the CAN frame itself "
            "contains no originating ECU or cause."
        )
        self._diagnostics.emit(
            code="safety.estop_frame",
            title=f"Safety stop frame · {bus.title()}",
            detail=detail,
            severity="critical",
            bus=bus,
            can_id=0x001,
            evidence={
                "cause": "can_estop_frame",
                "origin": origin,
                "host_source": source,
                "direction": frame.direction.value,
                "frame_source": frame.source.value,
                "dlc": frame.dlc,
                "session_id": self._get_session_id(),
            },
        )

    def _observe_rt_state(self, message: MessageState) -> None:
        mode = _signal_number(message, "mode")
        reason = _signal_number(message, "estop_reason") or 0
        mode_estop = mode == 2
        state = (mode_estop, reason)
        self._rt_state_by_bus[message.bus] = state

        if not mode_estop and reason == 0:
            any_bus_active = any(
                bus_mode_estop or bus_reason != 0
                for bus_mode_estop, bus_reason in self._rt_state_by_bus.values()
            )
            if not any_bus_active and self._last_rt_event is not None:
                self._diagnostics.emit(
                    code="safety.rt_estop_cleared",
                    title="RT safety stop cleared",
                    detail="RT_STATE_RPT returned to a non-ESTOP mode with reason 0.",
                    severity="info",
                    bus=message.bus,
                    can_id=message.can_id,
                    evidence={
                        "previous_mode_estop": self._last_rt_event[0],
                        "previous_reason": self._last_rt_event[1],
                    },
                )
                self._diagnostics.recover(
                    "safety.rt_estop",
                    scope=self._last_rt_event_bus or message.bus,
                    force=True,
                )
                self._last_rt_event = None
                self._last_rt_event_bus = None
            return

        if state == self._last_rt_event:
            return
        self._last_rt_event = state
        self._last_rt_event_bus = message.bus

        label = RT_ESTOP_REASONS.get(reason, f"unknown_{reason}")
        display = reason_display(reason)
        explanation = reason_detail(reason)
        correlated, source = self._host_correlation()
        if reason == 5:
            if correlated:
                explanation += f" This observation correlates with toolkit injection ({source})."
            else:
                explanation += " No recent toolkit injection was recorded; the external sender is unknown."
        detail = (
            f"RT entered/reported ESTOP; reason {reason}: {display}. {explanation}"
        )
        self._diagnostics.emit(
            code="safety.rt_estop",
            title=f"RT safety stop · {display}",
            detail=detail,
            severity="critical",
            bus=message.bus,
            can_id=message.can_id,
            evidence={
                "cause": label,
                "reason_code": reason,
                "reason_display": display,
                "mode": mode,
                "safety_state": _signal_number(message, "safety_state"),
                "host_correlated": correlated,
                "host_source": source,
                "session_id": self._get_session_id(),
            },
        )
