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

# RT firmware (rt-esp32/src/config.h) — estop_reason on RT_STATE_RPT.
RT_ESTOP_REASONS: dict[int, str] = {
    0: "none",
    2: "heartbeat_loss",
    3: "following_error",
    4: "obstacle",
    5: "can_estop_frame",
    6: "bus_off",
    7: "internal",
}


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
        _on(sys_safety, "estop_active")
        or _on(sys_hb, "estop_active")
        or _on(sys_diag, "estop_active")
    )
    sys_hb_bad = sys_hb is not None and _fresh_ok(sys_hb) and not _on(sys_hb, "heartbeat_ok")
    sys_can_bad = sys_hb is not None and _fresh_ok(sys_hb) and not _on(sys_hb, "can_ok")
    sys_brake_fault = _on(sys_diag, "brake_fault")

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
    rt_mode_estop = rt_mode == "ESTOP" or _num(rt, "mode") == 2
    reason_code = _num(rt, "estop_reason")
    if reason_code is None:
        reason_code = 0
    reason_label = RT_ESTOP_REASONS.get(reason_code, f"unknown_{reason_code}")
    safety_state = _num(rt, "safety_state")

    sources: list[dict[str, Any]] = []
    causes: list[str] = []

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
        detail = f"RT mode={rt_mode or '—'} · estop_reason={reason_code} ({reason_label})"
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
                "mode": rt_mode or None,
                "safety_state": safety_state,
            }
        )
        if reason_code != 0:
            causes.append(f"RT estop_reason={reason_code} ({reason_label})")
        elif rt_mode_estop:
            causes.append(f"RT mode ESTOP (reason code 0 / not set)")

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
    )

    if not any_active:
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
            "safety_state": safety_state,
            "frame_fresh": _fresh_ok(rt),
        },
        "sources": sources,
        "causes": causes,
        "summary": summary,
        "reason_codes": dict(RT_ESTOP_REASONS),
    }
