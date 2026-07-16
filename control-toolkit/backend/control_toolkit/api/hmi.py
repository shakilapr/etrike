"""HMI mode/power periodic control (1 Hz)."""

from __future__ import annotations

from fastapi import APIRouter, Request
from pydantic import BaseModel, Field

from control_toolkit.models.frames import FrameSource

router = APIRouter(prefix="/hmi", tags=["hmi"])

_MODE_JOB = "hmi_mode"
_PWR_JOB = "hmi_pwr"


class ModeBody(BaseModel):
    req_mode: int = Field(ge=0, le=1, description="0=MANUAL 1=AUTO")
    enabled: bool = True


class PowerBody(BaseModel):
    req_start: int = Field(ge=0, le=1, description="0=OFF 1=ON")
    enabled: bool = True


def _jobs(life) -> dict[str, str]:
    return getattr(life, "_hmi_jobs", {})


@router.post("/mode")
def set_mode(request: Request, body: ModeBody) -> dict:
    life = request.app.state.lifecycle
    life.sessions.require_bench_tx_enabled()
    prev = dict(_jobs(life))
    if _MODE_JOB in prev:
        life.scheduler.cancel(prev[_MODE_JOB])
        prev.pop(_MODE_JOB, None)
    if not body.enabled:
        life._hmi_jobs = prev
        return {"ok": True, "enabled": False}
    job_id = life.scheduler.schedule(
        bus="high",
        key="hmi:hmi_mode_req",
        values={"req_mode": body.req_mode, "rolling_counter": 0},
        period_ms=1000,
        owner="hmi:mode",
        source=FrameSource.INJECTION,
        counter_field="rolling_counter",
    )
    prev[_MODE_JOB] = job_id
    life._hmi_jobs = prev
    return {"ok": True, "enabled": True, "job_id": job_id, "req_mode": body.req_mode}


@router.post("/power")
def set_power(request: Request, body: PowerBody) -> dict:
    life = request.app.state.lifecycle
    life.sessions.require_bench_tx_enabled()
    prev = dict(_jobs(life))
    if _PWR_JOB in prev:
        life.scheduler.cancel(prev[_PWR_JOB])
        prev.pop(_PWR_JOB, None)
    if not body.enabled:
        life._hmi_jobs = prev
        return {"ok": True, "enabled": False}
    job_id = life.scheduler.schedule(
        bus="high",
        key="hmi:hmi_pwr_req",
        values={"req_start": body.req_start, "rolling_counter": 0},
        period_ms=1000,
        owner="hmi:power",
        source=FrameSource.INJECTION,
        counter_field="rolling_counter",
    )
    prev[_PWR_JOB] = job_id
    life._hmi_jobs = prev
    return {"ok": True, "enabled": True, "job_id": job_id, "req_start": body.req_start}
