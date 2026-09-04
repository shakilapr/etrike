"""Vehicle-mode / motion-gate derivation for control snapshots.

RT only emits non-zero Low-bus actuator frames (0x204 RT_DRIVE_CMD to MTR,
0x169 VCU_SES_REQ to SES, 0x7B9/0x205 to SEB) while the vehicle is in AUTO
(rt-esp32/src/main.cpp). In MANUAL it pins RT_DRIVE_CMD to {0, N} as a
keep-alive — the actuator units see no motion command.

This module derives the authoritative vehicle mode from the live bus (RT
RT_STATE_RPT / SYS_MODE_CMD) and reports whether an active Host motion
command is currently gated by mode. The Control Toolkit never changes TX
behavior based on this — it is pure observability so clients and tests can
tell "mode-gated" apart from "no command / bus down".
"""

from __future__ import annotations

from typing import Any

from control_toolkit.models.state import MessageState

_MODE_LABELS = {0: "MANUAL", 1: "AUTO", 2: "ESTOP"}


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


def _fresh(msg: MessageState | None, max_age_ms: float = 3000.0) -> bool:
    if msg is None:
        return False
    f = str(getattr(msg.freshness, "value", msg.freshness)).lower()
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


def derive_vehicle_mode(
    messages: list[MessageState],
) -> dict[str, Any]:
    """Derive the authoritative mode from RT_STATE_RPT / SYS_MODE_CMD.

    Returns ``{"mode", "source", "frame_fresh"}``. ``mode`` is the enum label
    (MANUAL/AUTO/ESTOP) when available, else the numeric value string.
    ``source`` is "rt" when RT_STATE_RPT is fresh, else "sys" from SYS_MODE_CMD,
    else "unavailable".
    """
    rt = _find(messages, "RT_STATE_RPT", "high") or _find(
        messages, "RT_STATE_RPT", "low"
    ) or _find(messages, "RT_STATE_RPT")
    rt_fresh = _fresh(rt)
    mode: Any = None
    if rt_fresh:
        raw = _sig(rt, "mode")
        mode = raw
        if mode is not None and not isinstance(mode, str):
            mode = _MODE_LABELS.get(_num(rt, "mode") or -1, str(mode))
        elif isinstance(mode, str):
            mode = mode.strip().upper() or None
        if mode is not None:
            return {"mode": mode, "source": "rt", "frame_fresh": True}

    sys_mode = _find(messages, "SYS_MODE_CMD", "low") or _find(
        messages, "SYS_MODE_CMD"
    )
    if _fresh(sys_mode):
        n = _num(sys_mode, "mode")
        if n is not None:
            return {
                "mode": _MODE_LABELS.get(n, str(n)),
                "source": "sys",
                "frame_fresh": True,
            }
    return {"mode": None, "source": "unavailable", "frame_fresh": bool(rt_fresh)}


def derive_motion_gate(
    control: dict[str, Any],
    messages: list[MessageState],
) -> dict[str, Any]:
    """Report whether an active Host motion command is gated by vehicle mode.

    ``gated`` is true when the backend is actively streaming a High-bus motion
    command (kinematics) while the authoritative vehicle mode is not AUTO, so
    RT will keep Low actuator frames at zero. The reason string explains why.
    """
    mode_info = derive_vehicle_mode(messages)
    mode = mode_info["mode"]

    active = bool(control.get("active"))
    method = control.get("method")
    shaped_speed = control.get("shaped_speed_mmps") or 0
    hard_brake = bool(control.get("hard_brake"))

    # Direct low-bus bypass is not gated — it drives 0x204/0x169/0x7B9 directly.
    motion_streaming = (
        active and method == "high_kinematics" and int(shaped_speed or 0) != 0
    )
    gated = motion_streaming and mode != "AUTO"

    if not motion_streaming:
        reason = None
    elif mode is None:
        reason = (
            "Host motion command active but RT/SYS mode is unavailable (bus down)."
        )
    elif mode == "ESTOP":
        reason = "Vehicle is in ESTOP — RT keeps Low actuator commands at zero."
    else:
        reason = (
            f"Vehicle is in {mode} — RT only commands Low actuator units in AUTO; "
            "request AUTO via HMI_MODE_REQ (SYS is the mode authority)."
        )

    return {
        "vehicle_mode": mode,
        "mode_source": mode_info["source"],
        "frame_fresh": mode_info["frame_fresh"],
        "gated": gated,
        "motion_streaming": motion_streaming,
        "hard_brake": hard_brake,
        "reason": reason,
    }
