"""Backend status endpoint (workplan §1.6).

``GET /api/v1/status`` — readiness, adapter state, protocol wire hash, active
profile, and session. Serves as the health probe and the frontend's protocol
hash-match check.
"""

from __future__ import annotations

from fastapi import APIRouter, Request

from vtc import __version__, protocol_bridge as proto
from vtc.models.adapter import AdapterStatus
from vtc.models.session import SessionState

router = APIRouter(tags=["status"])


@router.get("/status")
def get_status(request: Request) -> dict:
    lifecycle = request.app.state.lifecycle
    config = request.app.state.config
    # Real adapter status when a transport is open (Pure Software opens one at
    # startup); otherwise Absent. Session state machine lands in Phase 3.
    adapter = (
        lifecycle.transport.status()
        if lifecycle.transport is not None
        else AdapterStatus()
    )
    return {
        "service": config.title,
        "version": __version__,
        "ready": lifecycle.ready,
        "wire_hash": proto.WIRE_HASH,
        "profile": config.default_profile.value,
        "catalog": {
            "messages": proto.message_count(),
            "instances": proto.instance_count(),
        },
        "adapter": adapter.model_dump(),
        "session": SessionState(profile=config.default_profile).model_dump(),
    }
