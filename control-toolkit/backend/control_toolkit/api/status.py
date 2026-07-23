"""Backend status endpoint (workplan §1.6).

``GET /api/v1/status`` — readiness, adapter state, protocol wire hash, active
profile, and session. Serves as the health probe and the frontend's protocol
hash-match check.
"""

from __future__ import annotations

from fastapi import APIRouter, Request

from control_toolkit import __version__, protocol_bridge as proto
from control_toolkit.models.adapter import AdapterHealth, AdapterStatus
from control_toolkit.services.estop_report import build_estop_report

router = APIRouter(tags=["status"])


@router.get("/status")
def get_status(request: Request) -> dict:
    lifecycle = request.app.state.lifecycle
    config = request.app.state.config
    session = lifecycle.sessions.snapshot()
    # Real adapter status when a transport is open; otherwise Absent.
    # In Real mode with no USB, health stays absent so the UI can show
    # "no connection" without falling back to virtual traffic.
    if lifecycle.transport is not None:
        adapter = lifecycle.transport.status()
    else:
        dest = (session.destination or "").lower()
        last = None
        if dest == "physical":
            last = "CANalyst-II not connected — Real mode, no physical link"
        adapter = AdapterStatus(
            identity="none",
            health=AdapterHealth.ABSENT,
            last_error=last,
            channels={},  # explicit empty — UI must not keep virtual channel ghosts
        )
    # Active session profile is authoritative. Fall back to configured default
    # only when no session has ever set a profile (still always present on state).
    active_profile = (
        session.profile.value
        if getattr(session, "profile", None) is not None
        else config.default_profile.value
    )
    # ESTOP cause report from latest bus state + host latch (not latch alone).
    try:
        snap = lifecycle.latest.snapshot()
        msgs = list(snap.messages)
    except Exception:  # noqa: BLE001
        msgs = []
    estop = build_estop_report(
        msgs, host_latch=bool(getattr(session, "estop_active", False))
    )

    return {
        "service": config.title,
        "version": __version__,
        "ready": lifecycle.ready,
        "wire_hash": proto.WIRE_HASH,
        "semantic_hash": proto.SEMANTIC_HASH,
        "network_hash": proto.NETWORK_HASH,
        "profile": active_profile,
        "default_profile": config.default_profile.value,
        "catalog": {
            "messages": proto.message_count(),
            "instances": proto.instance_count(),
        },
        "adapter": adapter.model_dump(),
        "session": session.model_dump(),
        "estop": estop,
        "link": {
            "mode": "real" if (session.destination or "") == "physical" else "computer",
            "destination": session.destination,
            "connected": lifecycle.transport is not None
            and adapter.health.value
            in ("open", "active", "quiet", "degraded", "recovering"),
            "health": adapter.health.value,
            "detail": adapter.last_error,
        },
    }
