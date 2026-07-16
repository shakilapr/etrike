"""Analysis stimuli API — targeted inject, not full synthetic vehicle."""

from __future__ import annotations

from fastapi import APIRouter, Request
from pydantic import BaseModel, Field

from control_toolkit.models.frames import FrameSource
from control_toolkit.services.session_manager import SessionError

router = APIRouter(prefix="/analysis", tags=["analysis"])


class HostDriveBody(BaseModel):
    """HOST_DRIVE_CMD signals under study (high bus)."""

    speed_mmps: int = Field(default=0, ge=-500, le=3000)
    yaw_rate_mrad_s: int = Field(default=0, ge=-3000, le=3000)
    gear: int = Field(default=1, ge=0, le=3)
    period_ms: float | None = Field(
        default=None,
        description="If set, schedule periodic re-encode; else one-shot",
    )


@router.post("/host-drive")
def host_drive(request: Request, body: HostDriveBody) -> dict:
    """Inject or schedule Host drive command for yaw/speed analysis."""
    life = request.app.state.lifecycle
    life.sessions.require_bench_tx_enabled()
    values = {
        "speed_mmps": body.speed_mmps,
        "yaw_rate_mrad_s": body.yaw_rate_mrad_s,
        "gear": body.gear,
    }
    if body.period_ms is not None and body.period_ms > 0:
        started = life.synthetic.start_host_drive(
            speed_mmps=body.speed_mmps,
            yaw_rate_mrad_s=body.yaw_rate_mrad_s,
            gear=body.gear,
            period_ms=body.period_ms,
        )
        return {
            "ok": True,
            "mode": "periodic",
            "period_ms": body.period_ms,
            **started,
            "values": values,
        }

    result = life.tx_gate.submit(
        bus="high",
        key="host:host_drive_cmd",
        values=values,
        owner="analysis:host_drive",
        source=FrameSource.INJECTION,
    )
    if result.disposition == "rejected":
        raise SessionError(
            "analysis.rejected",
            result.reason or "rejected",
            status=409,
        )
    enc = result.encode
    return {
        "ok": True,
        "mode": "oneshot",
        "disposition": result.disposition,
        "request_id": result.request_id,
        "lease_id": result.lease_id,
        "can_id": enc.can_id if enc else 0x300,
        "data_hex": enc.data.hex() if enc else "",
        "values": values,
    }


@router.post("/stop")
def stop_analysis(request: Request) -> dict:
    n = request.app.state.lifecycle.synthetic.stop_all()
    return {"ok": True, "stopped": n}
