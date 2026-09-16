"""Structured ESTOP cause report from session + latest CAN state.

The UI historically only saw session.estop_active (host latch) or a coarse
"sources" string. This report exposes *why* ESTOP is active:

  - host inject latch
  - SAFETY_ESTOP 0x001 on High/Low
  - SYS estop_active / degraded heartbeat flags
  - RT mode ESTOP + estop_reason (firmware codes)
"""

from __future__ import annotations

from typing import Any

from control_toolkit.models.state import MessageState

try:
    from protocol.generated.python.diagnostics import REGISTRY as DIAG_REGISTRY

    DIAG_BY_ID: dict[int, dict[str, Any]] = {
        int(info["id"]): info for info in DIAG_REGISTRY.values() if "id" in info
    }
except Exception:
    DIAG_BY_ID = {}

DIAG_STATE_LABELS: dict[int, str] = {
    0: "PENDING",
    1: "ACTIVE",
    2: "LATCHED",
    3: "RECOVERED",
    4: "CLEARED",
}

# RT firmware (rt-esp32/src/config.h) — estop_reason on RT_STATE_RPT.
RT_ESTOP_REASONS: dict[int, str] = {
    0: "none",
    1: "button",
    2: "heartbeat_loss",
    3: "following_error",
    4: "obstacle",
    5: "can_estop_frame",
    6: "bus_off",
    7: "internal",
    8: "egas_mismatch",
    9: "stale_cmd",
    10: "watchdog",
}

RT_ESTOP_REASON_DISPLAY: dict[int, str] = {
    0: "None",
    1: "Physical ESTOP button pressed",
    2: "Heartbeat lost",
    3: "Steering following error",
    4: "Obstacle detected",
    5: "CAN ESTOP received",
    6: "CAN bus-off",
    7: "RT internal fault",
    8: "EGAS speed mismatch fault",
    9: "Stale command timeout",
    10: "Task watchdog fault",
}

RT_ESTOP_REASON_DETAIL: dict[int, str] = {
    0: "RT has not reported a safety-stop reason.",
    1: "RT or SYS stopped motion after the physical mushroom ESTOP button was pressed or circuit opened.",
    2: "RT stopped motion after a required node heartbeat (Host or SYS) timed out.",
    3: "RT stopped motion after steering exceeded the speed-scaled following-error limit for >300ms.",
    4: "RT stopped motion after an obstacle was detected in the stop distance zone.",
    5: (
        "RT stopped after receiving SAFETY_ESTOP (0x001). The frame has DLC 0 "
        "and sender=Any, so the wire protocol does not identify its origin."
    ),
    6: "RT stopped motion because a CAN controller entered bus-off error state.",
    7: "RT stopped motion because it detected an internal safety fault or L3 hardware error.",
    8: "RT stopped motion due to an EGAS motor speed setpoint vs feedback mismatch (>500 mm/s for >500ms).",
    9: "RT stopped motion because the control command stream timed out (stale command watchdog).",
    10: "RT stopped motion because an internal task watchdog failed to kick within its deadline.",
}


def reason_display(reason_code: int) -> str:
    return RT_ESTOP_REASON_DISPLAY.get(reason_code, f"Unknown reason {reason_code}")


def reason_detail(reason_code: int) -> str:
    return RT_ESTOP_REASON_DETAIL.get(
        reason_code, f"RT reported an undocumented ESTOP reason code ({reason_code})."
    )


def _sig(msg: MessageState | None, key: str) -> Any:
    if msg is None:
        return None
    s = msg.signals.get(key)
    if s is None:
        return None
    if s.enum_label is not None and str(s.enum_label) != "":
        return s.enum_label
    return s.engineering_value


def _num(msg: MessageState | None, key: str) -> int | None:
    v = _sig(msg, key)
    if v is None:
        return None
    try:
        return int(v)
    except (TypeError, ValueError):
        try:
            return int(float(v))
        except (TypeError, ValueError):
            return None


def _on(msg: MessageState | None, key: str) -> bool:
    v = _sig(msg, key)
    if v is None:
        return False
    if isinstance(v, bool):
        return v
    if isinstance(v, (int, float)):
        return v != 0
    t = str(v).strip().lower()
    if t in ("", "0", "false", "clear", "off", "inactive", "none"):
        return False
    if t in ("1", "true", "active", "on", "estop"):
        return True
    try:
        return float(t) != 0
    except ValueError:
        return True


def _fresh_ok(msg: MessageState | None, max_age_ms: float = 3000.0) -> bool:
    if msg is None:
        return False
    f = str(msg.freshness.value if hasattr(msg.freshness, "value") else msg.freshness).lower()
    if f in ("live", "late"):
        return True
    if f == "unseen":
        return False
    if msg.age_ms is not None and msg.age_ms <= max_age_ms:
        return True
    return False


def _find(
    messages: list[MessageState], name: str, bus: str | None = None
) -> MessageState | None:
    for m in messages:
        if m.name == name and (bus is None or m.bus == bus):
            return m
    return None


# NODE_STATUS family: (frame name, node label). Carries the persistent latch /
# authority state that the older signal frames only approximate.
NODE_STATUS_FRAMES: tuple[tuple[str, str], ...] = (
    ("SYS_NODE_STATUS", "sys"),
    ("RT_NODE_STATUS", "rt"),
    ("MTR_NODE_STATUS", "mtr"),
)


def _node_status(messages: list[MessageState], name: str) -> dict[str, Any] | None:
    """Extract the observational NODE_STATUS fields for one node (fresh only)."""
    msg = _find(messages, name, "low") or _find(messages, name)
    if msg is None or not _fresh_ok(msg):
        return None
    return {
        "state": _sig(msg, "node_state"),
        "estop_active": _on(msg, "estop_active"),
        "estop_latched": _on(msg, "estop_latched"),
        "recovery_pending": _on(msg, "recovery_pending"),
        "ready": _on(msg, "ready"),
        "degraded": _on(msg, "degraded"),
        "output_enabled": _on(msg, "output_enabled"),
        "block_mask": _num(msg, "block_mask"),
    }


def _node_estop_latched(status: dict[str, Any] | None) -> bool:
    if status is None:
        return False
    state = str(status.get("state") or "").strip().upper()
    return bool(status.get("estop_latched")) or bool(
        status.get("estop_active")
    ) or state == "ESTOP"


def build_estop_report(
    messages: list[MessageState],
    *,
    host_latch: bool = False,
) -> dict[str, Any]:
    """Return a JSON-ready ESTOP report for status / diagnostics UIs."""
    safety_h = _find(messages, "SAFETY_ESTOP", "high")
    safety_l = _find(messages, "SAFETY_ESTOP", "low")
    # SAFETY_ESTOP is event (cycle_ms=0) — freshness goes missing quickly; use age window.
    bus_high = _fresh_ok(safety_h, max_age_ms=5000.0)
    bus_low = _fresh_ok(safety_l, max_age_ms=5000.0)

    sys_safety = _find(messages, "SYS_SAFETY_STS", "low") or _find(
        messages, "SYS_SAFETY_STS"
    )
    sys_hb = _find(messages, "SYS_HEARTBEAT", "low") or _find(messages, "SYS_HEARTBEAT")
    sys_diag = _find(messages, "SYS_DIAG_RPT", "low") or _find(messages, "SYS_DIAG_RPT")

    sys_estop = (
        (_fresh_ok(sys_safety) and _on(sys_safety, "estop_active"))
        or (_fresh_ok(sys_hb) and _on(sys_hb, "estop_active"))
        or (_fresh_ok(sys_diag) and _on(sys_diag, "estop_active"))
    )
    sys_hb_bad = sys_hb is not None and _fresh_ok(sys_hb) and not _on(sys_hb, "heartbeat_ok")
    sys_can_bad = sys_hb is not None and _fresh_ok(sys_hb) and not _on(sys_hb, "can_ok")
    sys_brake_fault = _fresh_ok(sys_diag) and _on(sys_diag, "brake_fault")

    rt = (
        _find(messages, "RT_STATE_RPT", "high")
        or _find(messages, "RT_STATE_RPT", "low")
        or _find(messages, "RT_STATE_RPT")
    )
    rt_mode_raw = _sig(rt, "mode")
    rt_mode = str(rt_mode_raw or "").strip().upper() if rt_mode_raw is not None else ""
    if not rt_mode and _num(rt, "mode") is not None:
        mode_n = _num(rt, "mode")
        rt_mode = {0: "MANUAL", 1: "AUTO", 2: "ESTOP"}.get(mode_n or -1, str(mode_n))
    rt_fresh = _fresh_ok(rt)
    rt_mode_estop = rt_fresh and (rt_mode == "ESTOP" or _num(rt, "mode") == 2)
    reason_code = _num(rt, "estop_reason") if rt_fresh else 0
    if reason_code is None:
        reason_code = 0
    reason_label = RT_ESTOP_REASONS.get(reason_code, f"unknown_{reason_code}")
    reason_human = reason_display(reason_code)
    reason_explanation = reason_detail(reason_code)
    safety_state = _num(rt, "safety_state")

    # Ingest 0x621 RT_DIAG_EVENT_RPT (the designed diagnostic "why" channel)
    rt_diag_msg = (
        _find(messages, "RT_DIAG_EVENT_RPT", "high")
        or _find(messages, "RT_DIAG_EVENT_RPT", "low")
        or _find(messages, "RT_DIAG_EVENT_RPT")
    )
    rt_diag_events: list[dict[str, Any]] = []
    active_rt_diag_event: dict[str, Any] | None = None
    if rt_diag_msg is not None:
        diag_id = _num(rt_diag_msg, "diag_id")
        diag_state_num = _num(rt_diag_msg, "state") or 0
        diag_state_str = DIAG_STATE_LABELS.get(diag_state_num, f"STATE_{diag_state_num}")
        occ_count = _num(rt_diag_msg, "occurrence_count") or 0
        diag_info = DIAG_BY_ID.get(int(diag_id)) if diag_id is not None else None
        diag_key = str(diag_info.get("key", f"DIAG_{diag_id}")) if diag_info else f"DIAG_{diag_id}"
        diag_desc = str(diag_info.get("description", "")).strip() if diag_info else ""
        diag_sev = str(diag_info.get("severity", "WARNING")) if diag_info else "WARNING"

        ev_entry = {
            "diag_id": diag_id,
            "key": diag_key,
            "state": diag_state_str,
            "state_code": diag_state_num,
            "severity": diag_sev,
            "occurrences": occ_count,
            "age_ms": rt_diag_msg.age_ms,
            "description": diag_desc,
        }
        rt_diag_events.append(ev_entry)
        if diag_state_num in (1, 2):  # ACTIVE or LATCHED
            active_rt_diag_event = ev_entry

    sources: list[dict[str, Any]] = []
    causes: list[str] = []

    # NODE_STATUS (0x500/0x501/0x502): the authoritative persistent latch / node
    # authority view. A latched node must count as an active source even when the
    # older signal frames race ahead on the same ECU.
    nodes: dict[str, dict[str, Any]] = {}
    node_estop: dict[str, bool] = {}
    for frame_name, node_label in NODE_STATUS_FRAMES:
        status = _node_status(messages, frame_name)
        if status is None:
            continue
        nodes[node_label] = status
        node_estop[node_label] = _node_estop_latched(status)

    if node_estop.get("sys"):
        sources.append(
            {
                "id": "sys_node_latched",
                "active": True,
                "title": "SYS node ESTOP latched (NODE_STATUS)",
                "detail": "SYS_NODE_STATUS reports a latched ESTOP node_state / estop_latched",
            }
        )
        causes.append("SYS NODE_STATUS latched")
    if node_estop.get("rt"):
        sources.append(
            {
                "id": "rt_node_latched",
                "active": True,
                "title": "RT node ESTOP latched (NODE_STATUS)",
                "detail": "RT_NODE_STATUS reports a latched ESTOP node_state / estop_latched",
            }
        )
        causes.append("RT NODE_STATUS latched")
    if node_estop.get("mtr"):
        sources.append(
            {
                "id": "mtr_node_latched",
                "active": True,
                "title": "MTR node ESTOP latched (NODE_STATUS)",
                "detail": "MTR_NODE_STATUS reports a latched ESTOP node_state / estop_latched",
            }
        )
        causes.append("MTR NODE_STATUS latched")
    node_inhibited = [
        label
        for label, status in nodes.items()
        if str(status.get("state") or "").strip().upper() in ("INHIBITED", "RECOVER")
    ]
    if node_inhibited:
        causes.append(
            "NODE_STATUS inhibited/recovering: " + ", ".join(sorted(node_inhibited))
        )

    if host_latch:
        sources.append(
            {
                "id": "host_latch",
                "active": True,
                "title": "Host inject latch",
                "detail": "Session estop_active — host SAFETY_ESTOP inject not cleared",
            }
        )
        causes.append("Host inject latch (Clear latch only clears host side)")
    if bus_high:
        sources.append(
            {
                "id": "bus_0x001_high",
                "active": True,
                "title": "SAFETY_ESTOP on High",
                "detail": "Recent 0x001 on High bus",
                "bus": "high",
                "can_id": 0x001,
            }
        )
        causes.append("0x001 SAFETY_ESTOP seen on High")
    if bus_low:
        sources.append(
            {
                "id": "bus_0x001_low",
                "active": True,
                "title": "SAFETY_ESTOP on Low",
                "detail": "Recent 0x001 on Low bus",
                "bus": "low",
                "can_id": 0x001,
            }
        )
        causes.append("0x001 SAFETY_ESTOP seen on Low")
    if sys_estop:
        sources.append(
            {
                "id": "sys_estop_active",
                "active": True,
                "title": "SYS reports ESTOP",
                "detail": "estop_active on SYS_SAFETY_STS / SYS_HEARTBEAT / SYS_DIAG_RPT",
            }
        )
        causes.append("SYS estop_active flag")
    if sys_hb_bad:
        sources.append(
            {
                "id": "sys_heartbeat_bad",
                "active": True,
                "title": "SYS heartbeat not OK",
                "detail": "SYS_HEARTBEAT.heartbeat_ok=0 while frame is live",
            }
        )
        causes.append("SYS heartbeat_ok=0")
    if sys_can_bad:
        sources.append(
            {
                "id": "sys_can_bad",
                "active": True,
                "title": "SYS CAN not OK",
                "detail": "SYS_HEARTBEAT.can_ok=0 while frame is live",
            }
        )
        causes.append("SYS can_ok=0")
    if sys_brake_fault:
        sources.append(
            {
                "id": "sys_brake_fault",
                "active": True,
                "title": "SYS brake fault",
                "detail": "SYS_DIAG_RPT.brake_fault active",
            }
        )
        causes.append("SYS brake_fault")
    if rt_mode_estop or reason_code != 0:
        detail = (
            f"RT mode={rt_mode or '—'} · reason {reason_code}: {reason_human}. "
            f"{reason_explanation}"
        )
        if safety_state is not None:
            detail += f" · safety_state={safety_state}"
        sources.append(
            {
                "id": "rt_estop",
                "active": True,
                "title": "RT ESTOP / reason",
                "detail": detail,
                "estop_reason": reason_code,
                "estop_reason_label": reason_label,
                "estop_reason_display": reason_human,
                "estop_reason_detail": reason_explanation,
                "mode": rt_mode or None,
                "safety_state": safety_state,
            }
        )
        if reason_code != 0:
            causes.append(f"RT reason {reason_code}: {reason_human}")
        elif rt_mode_estop:
            causes.append("RT mode ESTOP (reason code 0 / not set)")

    any_active = bool(
        host_latch
        or bus_high
        or bus_low
        or sys_estop
        or rt_mode_estop
        or reason_code != 0
        or sys_hb_bad
        or sys_can_bad
        or sys_brake_fault
        or any(node_estop.values())
    )

    primary_cause = "No active safety stop"
    cause_resolution = "clear"
    if active_rt_diag_event is not None and (rt_mode_estop or reason_code != 0 or any(node_estop.values())):
        primary_cause = f"RT: {active_rt_diag_event['key']} ({active_rt_diag_event['state']})"
        cause_resolution = "reported"
    elif reason_code in (1, 2, 3, 4, 6, 7, 8, 9, 10):
        primary_cause = f"RT: {reason_human}"
        cause_resolution = "reported"
    elif reason_code == 5 and host_latch:
        primary_cause = "Host/toolkit SAFETY_ESTOP injection received by RT"
        cause_resolution = "correlated"
    elif reason_code == 5:
        primary_cause = (
            "RT received SAFETY_ESTOP (0x001); originating node is not encoded"
        )
        cause_resolution = "unknown_origin"
    elif sys_hb_bad:
        primary_cause = "SYS detected a required heartbeat failure"
        cause_resolution = "reported"
    elif sys_brake_fault:
        primary_cause = "SYS brake fault"
        cause_resolution = "reported"
    elif host_latch:
        primary_cause = "Host/toolkit SAFETY_ESTOP injection latch"
        cause_resolution = "reported"
    elif bus_high or bus_low:
        primary_cause = (
            "SAFETY_ESTOP (0x001) observed; originating node is not encoded"
        )
        cause_resolution = "unknown_origin"
    elif sys_estop or rt_mode_estop:
        primary_cause = "ECU reports ESTOP without a specific reason"
        cause_resolution = "unknown"
    elif any(node_estop.values()):
        latched = [label for label, active in node_estop.items() if active]
        primary_cause = "Latched ESTOP in NODE_STATUS: " + ", ".join(sorted(latched))
        cause_resolution = "reported"

    has_traffic = len(messages) > 0

    if not any_active:
        if not has_traffic and not host_latch:
            primary_cause = "No bus telemetry"
            cause_resolution = "unconfirmed"
            summary = "No bus telemetry — safety state unconfirmed"
        else:
            summary = "ESTOP clear — no host latch, no recent 0x001, SYS/RT not reporting ESTOP"
    elif causes:
        summary = "ESTOP active: " + "; ".join(causes)
    else:
        summary = "ESTOP active (sources listed)"

    return {
        "active": any_active,
        "host_latch": host_latch,
        "bus": {"high_0x001": bus_high, "low_0x001": bus_low},
        "sys": {
            "estop_active": sys_estop,
            "heartbeat_ok": None if sys_hb is None else _on(sys_hb, "heartbeat_ok"),
            "can_ok": None if sys_hb is None else _on(sys_hb, "can_ok"),
            "brake_fault": sys_brake_fault,
        },
        "rt": {
            "mode": rt_mode or None,
            "mode_estop": rt_mode_estop,
            "estop_reason": reason_code,
            "estop_reason_label": reason_label,
            "estop_reason_display": reason_human,
            "estop_reason_detail": reason_explanation,
            "safety_state": safety_state,
            "frame_fresh": rt_fresh,
            "diag_events": rt_diag_events,
        },
        "sources": sources,
        "causes": causes,
        "primary_cause": primary_cause,
        "cause_resolution": cause_resolution,
        "summary": summary,
        "reason_codes": dict(RT_ESTOP_REASONS),
        "nodes": nodes,
    }
