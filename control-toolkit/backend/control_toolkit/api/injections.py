"""Injection preview / one-shot / periodic API."""

from __future__ import annotations

from typing import Any

from fastapi import APIRouter, Request
from pydantic import BaseModel, Field

from control_toolkit.models.frames import FrameSource
from control_toolkit.services.encoder import encode_message
from control_toolkit.services.session_manager import SessionError

router = APIRouter(prefix="/injections", tags=["injections"])


class InjectBody(BaseModel):
    bus: str
    key: str | None = None
    can_id: int | None = None
    values: dict[str, Any] = Field(default_factory=dict)
    period_ms: float | None = None
    counter_field: str | None = None
    owner: str = "injection"


@router.post("/preview")
def preview(body: InjectBody) -> dict:
    result = encode_message(
        key=body.key, bus=body.bus, can_id=body.can_id, values=body.values
    )
    return {
        "ok": result.ok,
        "status": result.status,
        "bus": result.bus,
        "can_id": result.can_id,
        "key": result.key,
        "name": result.name,
        "dlc": result.dlc,
        "data_hex": result.data.hex(),
        "signals": result.signals,
        "warnings": result.warnings,
    }


@router.post("")
def inject(request: Request, body: InjectBody) -> dict:
    life = request.app.state.lifecycle
    life.sessions.require_bench_tx_enabled()

    if body.period_ms is not None and body.period_ms > 0:
        job_id = life.scheduler.schedule(
            bus=body.bus,
            key=body.key,
            can_id=body.can_id,
            values=body.values,
            period_ms=body.period_ms,
            owner=body.owner,
            source=FrameSource.INJECTION,
            counter_field=body.counter_field,
        )
        life.audit.log(
            category="inject",
            code="inject.scheduled",
            title="Periodic inject scheduled",
            detail=f"{body.key or body.can_id} @ {body.period_ms} ms",
            bus=body.bus,
            can_id=body.can_id,
            data={"job_id": job_id, "owner": body.owner, "values": body.values},
        )
        return {
            "ok": True,
            "disposition": "scheduled",
            "job_id": job_id,
            "period_ms": body.period_ms,
        }

    result = life.tx_gate.submit(
        bus=body.bus,
        key=body.key,
        can_id=body.can_id,
        values=body.values,
        owner=body.owner,
        source=FrameSource.INJECTION,
    )
    if result.disposition == "rejected":
        life.audit.log(
            category="inject",
            code="inject.rejected",
            title="Inject rejected",
            detail=result.reason or "rejected",
            severity="warning",
            bus=body.bus,
            can_id=body.can_id,
        )
        raise SessionError(
            "injection.rejected",
            result.reason or "rejected",
            status=409,
        )
    enc = result.encode
    life.audit.log(
        category="inject",
        code="inject.submitted",
        title="Inject submitted",
        detail=f"{enc.name if enc else body.key} 0x{(enc.can_id if enc else body.can_id or 0):X}",
        bus=enc.bus if enc else body.bus,
        can_id=enc.can_id if enc else body.can_id,
        data={
            "data_hex": enc.data.hex() if enc else "",
            "owner": body.owner,
            "request_id": result.request_id,
        },
    )
    return {
        "ok": True,
        "disposition": result.disposition,
        "request_id": result.request_id,
        "lease_id": result.lease_id,
        "bus": enc.bus if enc else body.bus,
        "can_id": enc.can_id if enc else body.can_id,
        "data_hex": enc.data.hex() if enc else "",
        "name": enc.name if enc else None,
    }


@router.delete("/{job_id}")
def cancel_injection(job_id: str, request: Request) -> dict:
    ok = request.app.state.lifecycle.scheduler.cancel(job_id)
    if not ok:
        raise SessionError("injection.not_found", f"job {job_id} not found", status=404)
    return {"ok": True, "job_id": job_id, "canceled": True}
