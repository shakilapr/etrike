"""Latest-state snapshot endpoint (workplan §1.6).

``GET /api/v1/state`` — atomic latest-state snapshot with a monotonic sequence.
Phase 1 returns an empty-but-valid snapshot; the router (§1.4) populates it.
"""

from __future__ import annotations

from fastapi import APIRouter, Request

from control_toolkit.models.state import LatestStateSnapshot

router = APIRouter(tags=["state"])


@router.get("/state", response_model=LatestStateSnapshot)
def get_state(request: Request) -> LatestStateSnapshot:
    return request.app.state.lifecycle.latest.snapshot()
