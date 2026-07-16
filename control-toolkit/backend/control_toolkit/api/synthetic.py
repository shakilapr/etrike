"""Synthetic peer control API."""

from __future__ import annotations

from fastapi import APIRouter, Request
from pydantic import BaseModel, Field

from control_toolkit.services.session_manager import SessionError
from control_toolkit.services.synthetic_peers import DEFAULT_PEERS

router = APIRouter(prefix="/synthetic-peers", tags=["synthetic-peers"])


class StartPeersBody(BaseModel):
    names: list[str] | None = None


class StopPeersBody(BaseModel):
    names: list[str] | None = None


@router.get("")
def list_peers(request: Request) -> dict:
    available = [
        {
            "name": p.name,
            "bus": p.bus,
            "key": p.key,
            "period_ms": p.period_ms,
        }
        for p in DEFAULT_PEERS
    ]
    running = request.app.state.lifecycle.synthetic.list_running()
    return {"available": available, "running": running}


@router.post("/start")
def start_peers(request: Request, body: StartPeersBody | None = None) -> dict:
    """Start named analysis stimuli only — never a full peer set by default."""
    request.app.state.lifecycle.sessions.require_bench_tx_enabled()
    names = body.names if body else None
    if not names:
        raise SessionError(
            "synthetic.names_required",
            "pass explicit stimulus names (no full-vehicle peer auto-start)",
            status=400,
        )
    known = {p.name for p in DEFAULT_PEERS}
    unknown = [n for n in names if n not in known]
    if unknown:
        raise SessionError(
            "synthetic.unknown",
            f"unknown stimuli: {unknown}",
            status=400,
        )
    started = request.app.state.lifecycle.synthetic.start(names)
    return {"started": started}


@router.post("/stop")
def stop_peers(request: Request, body: StopPeersBody | None = None) -> dict:
    names = body.names if body else None
    n = request.app.state.lifecycle.synthetic.stop(names)
    return {"stopped": n}
