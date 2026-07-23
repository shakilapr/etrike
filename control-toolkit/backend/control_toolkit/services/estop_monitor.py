"""Persist safety-stop observations as diagnostic and audit events.

The latest-state ESTOP report is intentionally a snapshot. This monitor records
the transition evidence that would otherwise disappear when an event frame
ages out of the live view.

Covers all failure sources that trigger or report ESTOP:
  - Host SAFETY_ESTOP injection (software latch)
  - Raw 0x001 SAFETY_ESTOP frames (High & Low CAN)
  - RT_STATE_RPT mode ESTOP & firmware reason codes (1..10)
  - SYS_SAFETY_STS / SYS_HEARTBEAT / SYS_DIAG_RPT flags (estop, heartbeat_ok, can_ok, brake_fault, wdt, egas)
  - SES_ERR_INFO 0x202 (L3 steering hardware fault bits)
"""

from __future__ import annotations

import logging
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

logger = logging.getLogger("control_toolkit.safety")


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


def _signal_bool(message: MessageState, key: str) -> bool | None:
    signal = message.signals.get(key)
    if signal is None:
        return None
    value = signal.engineering_value
    if value is None:
        return None
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    t = str(value).strip().lower()
    if t in ("1", "true", "active", "on", "estop", "fault"):
        return True
    if t in ("0", "false", "clear", "off", "inactive", "none", "ok"):
        return False
    return None


class EstopEventMonitor:
    """Turn raw 0x001 frames and ECU failure transitions into durable evidence and logs."""

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

        self._sys_estop_by_bus: dict[str, bool] = {}
        self._sys_hb_ok_by_bus: dict[str, bool] = {}
        self._sys_can_ok_by_bus: dict[str, bool] = {}
        self._sys_brake_fault_by_bus: dict[str, bool] = {}
        self._sys_wdt_fault_by_bus: dict[str, bool] = {}
        self._sys_egas_fault_by_bus: dict[str, bool] = {}

        self._ses_fault_by_bus: dict[str, int] = {}

    def note_host_inject(self, *, source: str) -> None:
        """Correlate the next observed 0x001 with an explicit toolkit action and log."""
        self._host_inject_at = time.monotonic()
        self._host_inject_source = source
        logger.warning("Host SAFETY_ESTOP injected from %s", source)
        self._diagnostics.emit(
            code="safety.host_estop_injected",
            title="Host ESTOP injected",
            detail=f"Host/toolkit injected 0x001 SAFETY_ESTOP frame from {source}",
            severity="warning",
            evidence={
                "source": source,
                "session_id": self._get_session_id(),
            },
        )

    def reset(self) -> None:
        self._host_inject_at = None
        self._host_inject_source = None
        self._rt_state_by_bus.clear()
        self._sys_estop_by_bus.clear()
        self._sys_hb_ok_by_bus.clear()
        self._sys_can_ok_by_bus.clear()
        self._sys_brake_fault_by_bus.clear()
        self._sys_wdt_fault_by_bus.clear()
        self._sys_egas_fault_by_bus.clear()
        self._ses_fault_by_bus.clear()
        self._last_rt_event = None
        self._last_rt_event_bus = None

    def observe(self, message: MessageState, frame: RawFrameEnvelope) -> None:
        if message.name == "SAFETY_ESTOP":
            self._observe_estop_frame(frame)
        elif message.name == "RT_STATE_RPT":
            self._observe_rt_state(message)
        elif message.name in ("SYS_SAFETY_STS", "SYS_HEARTBEAT", "SYS_DIAG_RPT"):
            self._observe_sys_state(message, frame)
        elif message.name == "SES_ERR_INFO":
            self._observe_ses_err_info(message, frame)

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
        logger.critical("SAFETY_ESTOP (0x001) frame observed on %s bus (%s)", bus.title(), origin_text)
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
                logger.info("RT safety stop cleared on %s bus", message.bus.title())
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
        logger.critical(
            "RT ESTOP active on %s bus: reason %d (%s). %s",
            message.bus.title(),
            reason,
            display,
            explanation,
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

    def _observe_sys_state(self, message: MessageState, frame: RawFrameEnvelope) -> None:
        bus = message.bus

        # Check estop_active
        estop_act = _signal_bool(message, "estop_active")
        if estop_act is None:
            estop_act = _signal_bool(message, "estop")
        prev_estop = self._sys_estop_by_bus.get(bus)
        if estop_act is not None:
            self._sys_estop_by_bus[bus] = estop_act
            if estop_act and not prev_estop:
                detail = (
                    f"SYS node reported ESTOP active on {bus.title()} bus "
                    f"({message.name})."
                )
                logger.critical("%s", detail)
                self._diagnostics.emit(
                    code="safety.sys_estop",
                    title=f"SYS safety stop active · {bus.title()}",
                    detail=detail,
                    severity="critical",
                    bus=bus,
                    can_id=message.can_id,
                    evidence={
                        "cause": "sys_estop_active",
                        "message_name": message.name,
                        "session_id": self._get_session_id(),
                    },
                )
            elif not estop_act and prev_estop:
                logger.info("SYS safety stop cleared on %s bus", bus.title())
                self._diagnostics.emit(
                    code="safety.sys_estop_cleared",
                    title=f"SYS safety stop cleared · {bus.title()}",
                    detail="SYS estop_active flag returned to inactive.",
                    severity="info",
                    bus=bus,
                    can_id=message.can_id,
                )
                self._diagnostics.recover("safety.sys_estop", scope=bus, force=True)

        # Check heartbeat_ok
        hb_ok = _signal_bool(message, "heartbeat_ok")
        if hb_ok is not None:
            prev_hb = self._sys_hb_ok_by_bus.get(bus)
            self._sys_hb_ok_by_bus[bus] = hb_ok
            if not hb_ok and prev_hb is not False:
                detail = (
                    f"SYS node reported heartbeat_ok=0 on {bus.title()} bus "
                    f"({message.name}). SYS task watchdog or peer heartbeat failed."
                )
                logger.error("%s", detail)
                self._diagnostics.emit(
                    code="safety.sys_heartbeat_failed",
                    title=f"SYS heartbeat failed · {bus.title()}",
                    detail=detail,
                    severity="error",
                    bus=bus,
                    can_id=message.can_id,
                    evidence={
                        "cause": "sys_heartbeat_failed",
                        "session_id": self._get_session_id(),
                    },
                )
            elif hb_ok and prev_hb is False:
                self._diagnostics.recover("safety.sys_heartbeat_failed", scope=bus, force=True)

        # Check can_ok
        can_ok = _signal_bool(message, "can_ok")
        if can_ok is not None:
            prev_can = self._sys_can_ok_by_bus.get(bus)
            self._sys_can_ok_by_bus[bus] = can_ok
            if not can_ok and prev_can is not False:
                detail = (
                    f"SYS node reported can_ok=0 on {bus.title()} bus "
                    f"({message.name}). SYS CAN controller error state."
                )
                logger.error("%s", detail)
                self._diagnostics.emit(
                    code="safety.sys_can_failed",
                    title=f"SYS CAN error · {bus.title()}",
                    detail=detail,
                    severity="error",
                    bus=bus,
                    can_id=message.can_id,
                    evidence={
                        "cause": "sys_can_failed",
                        "session_id": self._get_session_id(),
                    },
                )
            elif can_ok and prev_can is False:
                self._diagnostics.recover("safety.sys_can_failed", scope=bus, force=True)

        # Check brake_fault
        brake_f = _signal_bool(message, "brake_fault")
        if brake_f is not None:
            prev_brake = self._sys_brake_fault_by_bus.get(bus)
            self._sys_brake_fault_by_bus[bus] = brake_f
            if brake_f and not prev_brake:
                detail = (
                    f"SYS node reported brake_fault active on {bus.title()} bus. "
                    "Hydraulic SEB brake actuator or stroke sensor feedback error."
                )
                logger.critical("%s", detail)
                self._diagnostics.emit(
                    code="safety.sys_brake_fault",
                    title=f"SYS brake fault · {bus.title()}",
                    detail=detail,
                    severity="critical",
                    bus=bus,
                    can_id=message.can_id,
                    evidence={
                        "cause": "sys_brake_fault",
                        "session_id": self._get_session_id(),
                    },
                )
            elif not brake_f and prev_brake:
                self._diagnostics.recover("safety.sys_brake_fault", scope=bus, force=True)

        # Check task_wdt_fault / sys_wdt_fault
        wdt_f = _signal_bool(message, "task_wdt_fault")
        if wdt_f is None:
            wdt_f = _signal_bool(message, "sys_wdt_fault")
        if wdt_f is not None:
            prev_wdt = self._sys_wdt_fault_by_bus.get(bus)
            self._sys_wdt_fault_by_bus[bus] = wdt_f
            if wdt_f and not prev_wdt:
                detail = f"SYS node reported task watchdog fault active on {bus.title()} bus."
                logger.critical("%s", detail)
                self._diagnostics.emit(
                    code="safety.sys_wdt_fault",
                    title=f"SYS watchdog fault · {bus.title()}",
                    detail=detail,
                    severity="critical",
                    bus=bus,
                    can_id=message.can_id,
                    evidence={
                        "cause": "sys_wdt_fault",
                        "session_id": self._get_session_id(),
                    },
                )
            elif not wdt_f and prev_wdt:
                self._diagnostics.recover("safety.sys_wdt_fault", scope=bus, force=True)

        # Check throttle_fault / egas_fault
        egas_f = _signal_bool(message, "throttle_fault")
        if egas_f is None:
            egas_f = _signal_bool(message, "egas_fault")
        if egas_f is not None:
            prev_egas = self._sys_egas_fault_by_bus.get(bus)
            self._sys_egas_fault_by_bus[bus] = egas_f
            if egas_f and not prev_egas:
                detail = f"SYS node reported EGAS throttle plausibility fault active on {bus.title()} bus."
                logger.critical("%s", detail)
                self._diagnostics.emit(
                    code="safety.sys_egas_fault",
                    title=f"SYS EGAS throttle fault · {bus.title()}",
                    detail=detail,
                    severity="critical",
                    bus=bus,
                    can_id=message.can_id,
                    evidence={
                        "cause": "sys_egas_fault",
                        "session_id": self._get_session_id(),
                    },
                )
            elif not egas_f and prev_egas:
                self._diagnostics.recover("safety.sys_egas_fault", scope=bus, force=True)

    def _observe_ses_err_info(self, message: MessageState, frame: RawFrameEnvelope) -> None:
        bus = message.bus
        ang_f = _signal_number(message, "angle_faults") or 0
        tq_f = _signal_number(message, "torque_faults") or 0
        sys_f = _signal_number(message, "system_faults") or 0
        total_faults = ang_f | tq_f | sys_f

        prev_faults = self._ses_fault_by_bus.get(bus, 0)
        self._ses_fault_by_bus[bus] = total_faults

        if total_faults != 0 and total_faults != prev_faults:
            detail = (
                f"SES/EPS-C (0x202) reported L3 hardware fault on {bus.title()} bus: "
                f"angle_faults={ang_f:#x}, torque_faults={tq_f:#x}, system_faults={sys_f:#x}. "
                "Steering hardware fault triggers RT internal ESTOP."
            )
            logger.critical("%s", detail)
            self._diagnostics.emit(
                code="safety.ses_steering_fault",
                title=f"Steering unit L3 fault · {bus.title()}",
                detail=detail,
                severity="critical",
                bus=bus,
                can_id=0x202,
                evidence={
                    "cause": "ses_steering_fault",
                    "angle_faults": ang_f,
                    "torque_faults": tq_f,
                    "system_faults": sys_f,
                    "session_id": self._get_session_id(),
                },
            )
        elif total_faults == 0 and prev_faults != 0:
            self._diagnostics.recover("safety.ses_steering_fault", scope=bus, force=True)
