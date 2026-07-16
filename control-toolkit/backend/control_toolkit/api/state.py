"""Latest-state, history, and topology endpoints (workplan §1.5–1.6)."""

from __future__ import annotations

from fastapi import APIRouter, Query, Request

from control_toolkit.models.state import LatestStateSnapshot

router = APIRouter(tags=["state"])


@router.get("/state", response_model=LatestStateSnapshot)
def get_state(request: Request) -> LatestStateSnapshot:
    return request.app.state.lifecycle.latest.snapshot()


@router.get("/history")
def get_history(
    request: Request,
    limit: int = Query(default=200, ge=1, le=4096),
) -> dict:
    life = request.app.state.lifecycle
    frames = life.history.snapshot(limit=limit)
    return {
        "metrics": life.history.metrics(),
        "frames": [
            {
                "global_sequence": f.global_sequence,
                "channel_sequence": f.channel_sequence,
                "bus": f.channel.value,
                "can_id": f.can_id,
                "dlc": f.dlc,
                "data_hex": f.data.hex(),
                "is_extended": f.is_extended,
                "direction": f.direction.value,
                "source": f.source.value,
                "backend_arrival_ns": f.backend_arrival_ns,
                "adapter_epoch": f.adapter_epoch,
            }
            for f in frames
        ],
    }


@router.get("/topology")
def get_topology(request: Request) -> dict:
    snap = request.app.state.lifecycle.topology.snapshot()
    return snap.model_dump()
