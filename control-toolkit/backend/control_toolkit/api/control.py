"""Interactive control intent API (Phase 7 — virtual teleop)."""

from __future__ import annotations

from typing import Any

from fastapi import APIRouter, Request
from pydantic import BaseModel, Field

from control_toolkit.models.frames import FrameSource
from control_toolkit.services.session_manager import SessionError

router = APIRouter(prefix="/control", tags=["control"])


class IntentBody(BaseModel):
    sequence: int = Field(ge=0)
    source: str = "keyboard"
    mode: str = "kinematics"
    throttle: float = Field(default=0.0, ge=-1.0, le=1.0)
    steer: float = Field(default=0.0, ge=-1.0, le=1.0)
    gear: int | None = Field(default=None, ge=0, le=3)
    hard_brake: bool = False
    estop: bool = False


class ReleaseBody(BaseModel):
    reason: str = "client_release"


class DirectBody(BaseModel):
    """Low-bus actuator stream: motor (0x204), steering (0x169), brake (0x7B9)."""

    channel: str  # motor | steering | brake
    enabled: bool = True
    values: dict[str, Any] = Field(default_factory=dict)
    period_ms: float | None = None


@router.get("/status")
def control_status(request: Request) -> dict:
    return {"control": request.app.state.lifecycle.control.snapshot()}


@router.get("/estop")
def get_estop(request: Request) -> dict:
    """Structured ESTOP active/causes report (host latch + bus + SYS + RT reason)."""
    from control_toolkit.services.estop_report import build_estop_report

    life = request.app.state.lifecycle
    session = life.sessions.snapshot()
    try:
        msgs = list(life.latest.snapshot().messages)
    except Exception:  # noqa: BLE001
        msgs = []
    report = build_estop_report(
        msgs, host_latch=bool(getattr(session, "estop_active", False))
    )
    return {"estop": report, "session_id": session.session_id}


@router.post("/estop/clear")
def clear_estop(request: Request) -> dict:
    """Clear host-side ESTOP injection latch (not a claim that hardware recovered)."""
    from control_toolkit.services.estop_report import build_estop_report

    life = request.app.state.lifecycle
    st = life.sessions.clear_estop_latch()
    snap = life.control.clear_estop_flag()
    try:
        msgs = list(life.latest.snapshot().messages)
    except Exception:  # noqa: BLE001
        msgs = []
    remaining = build_estop_report(msgs, host_latch=False)
    detail = (
        "Operator cleared host inject latch. "
        + (
            f"Vehicle/bus ESTOP may still be active: {remaining['summary']}"
            if remaining.get("active")
            else "No SYS/RT/bus ESTOP sources active in latest state."
        )
    )
    life.diagnostics.emit(
        code="control.estop_cleared",
        title="Host ESTOP latch cleared",
        detail=detail,
        severity="info",
        evidence={"remaining": remaining},
    )
    life.audit.log(
        category="safety",
        code="control.estop_cleared",
        title="Host ESTOP latch cleared",
        detail=detail,
        severity="info",
        session_id=st.session_id,
        data={"remaining_active": remaining.get("active"), "causes": remaining.get("causes")},
    )
    return {"control": snap, "session": st.model_dump(), "estop": remaining}


@router.post("/intent")
def control_intent(request: Request, body: IntentBody) -> dict:
    life = request.app.state.lifecycle
    if body.estop:
        life.sessions.require_bench_tx_enabled()
        life.estop_monitor.note_host_inject(source=body.source)
        # Dual-bus ESTOP matches network.yaml (high↔low same_frame bridge).
        results = []
        for bus in ("high", "low"):
            r = life.tx_gate.submit(
                bus=bus,
                key="safety:safety_estop",
                values={},
                owner="control:estop",
                source=FrameSource.INJECTION,
                claim_ownership=False,
            )
            results.append(
                {
                    "bus": bus,
                    "disposition": r.disposition,
                    "reason": getattr(r, "reason", None),
                }
            )
        life.sessions.update_vehicle_view(estop_active=True)
        bits = ", ".join(f"{x['bus']}={x['disposition']}" for x in results)
        detail = (
            f"Host inject SAFETY_ESTOP (DLC=0) on high+low · TX [{bits}] · "
            f"source={body.source} · host latch ON until Clear · "
            f"does not clear ECU-latched ESTOP"
        )
        life.diagnostics.emit(
            code="control.estop",
            title="Control ESTOP inject",
            detail=detail,
            severity="critical",
            evidence={
                "source": body.source,
                "tx": results,
                "cause": "host_inject",
                "estop_reason_note": "Host inject; RT estop_reason may later show can_estop_frame(5)",
            },
        )
        life.audit.log(
            category="safety",
            code="control.estop",
            title="Control ESTOP inject",
            detail=detail,
            severity="critical",
            data={"source": body.source, "tx": results, "cause": "host_inject"},
        )
        snap = life.control.release(reason="estop")
        return {"control": snap, "estop": results, "cause": "host_inject", "detail": detail}

    try:
        snap = life.control.apply_intent(
            sequence=body.sequence,
            source=body.source,
            mode=body.mode,
            throttle=body.throttle,
            steer=body.steer,
            gear=body.gear,
            hard_brake=body.hard_brake,
            estop=False,
            profile=life.sessions.active_profile().value,
        )
    except SessionError:
        raise
    # Log sparse samples only (not every 50 ms key tick) — first arm + hard brake.
    if body.hard_brake or body.sequence <= 2:
        life.audit.log(
            category="control",
            code="control.intent",
            title="Host intent",
            detail=(
                f"src={body.source} thr={body.throttle:.2f} str={body.steer:.2f} "
                f"gear={snap.get('gear')} shaped={snap.get('shaped_speed_mmps')}"
            ),
            severity="info",
            data={"sequence": body.sequence, "method": snap.get("method")},
        )
    return {"control": snap}


@router.post("/release")
def control_release(request: Request, body: ReleaseBody | None = None) -> dict:
    reason = body.reason if body else "client_release"
    life = request.app.state.lifecycle
    snap = life.control.release(reason=reason)
    life.audit.log(
        category="control",
        code="control.release",
        title="Control released",
        detail=reason,
        severity="info",
    )
    return {"control": snap}


@router.post("/direct")
def control_direct(request: Request, body: DirectBody) -> dict:
    """Direct actuator TX on Low bus (exclusive with kinematics)."""
    life = request.app.state.lifecycle
    if body.enabled and life.sessions.active_profile().value == "full_vehicle":
        raise SessionError(
            "control.profile_blocked",
            "Low-bus direct actuator bypass is disabled in Full Vehicle profile",
            status=409,
        )
    snap = life.control.set_direct(
        channel=body.channel,
        enabled=body.enabled,
        values=body.values,
        period_ms=body.period_ms,
    )
    life.audit.log(
        category="control",
        code="control.direct." + ("start" if body.enabled else "stop"),
        title=f"Direct {body.channel} {'start' if body.enabled else 'stop'}",
        detail=str(body.values or {}),
        severity="info",
        bus="low",
        data={"channel": body.channel, "enabled": body.enabled},
    )
    return {"control": snap}
