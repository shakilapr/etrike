"""Synthetic peer control API."""

from __future__ import annotations

from typing import Any

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


class FeedMessageItem(BaseModel):
    bus: str
    can_id: int
    key: str | None = None
    name: str | None = None
    expected_rate_hz: float | None = None
    signals: dict[str, Any] = Field(default_factory=dict)


class FeedStateBody(BaseModel):
    messages: list[FeedMessageItem] = Field(default_factory=list)


@router.post("/feed")
def feed_state(request: Request, body: FeedStateBody) -> dict:
    """Inject decoded state messages directly into the latest-value store for dummy CAN simulation."""
    import time
    from control_toolkit.models.state import FreshnessState, MessageState, SignalValue

    life = request.app.state.lifecycle
    now_ns = time.monotonic_ns()
    upserted = 0
    for item in body.messages:
        sig_dict = {}
        for k, v in item.signals.items():
            if isinstance(v, dict):
                sig_dict[k] = SignalValue(**v)
            elif isinstance(v, (int, float)):
                sig_dict[k] = SignalValue(engineering_value=v, raw_value=v)
            else:
                sig_dict[k] = SignalValue(engineering_value=str(v), enum_label=str(v))
        st = MessageState(
            bus=item.bus,
            can_id=item.can_id,
            key=item.key,
            name=item.name,
            last_seen_ns=now_ns,
            freshness=FreshnessState.LIVE,
            expected_rate_hz=item.expected_rate_hz or 20.0,
            observed_rate_hz=item.expected_rate_hz or 20.0,
            validation_status="ok",
            signals=sig_dict,
        )
        life.latest.upsert(st)
        upserted += 1
    return {"ok": True, "upserted": upserted}

