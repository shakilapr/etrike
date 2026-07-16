"""Diagnostic events API (Phase 6)."""

from __future__ import annotations

from fastapi import APIRouter, Query, Request

from control_toolkit.services.session_manager import SessionError

router = APIRouter(tags=["events"])


@router.get("/events")
def list_events(
    request: Request,
    limit: int = Query(default=100, ge=1, le=1000),
    code: str | None = None,
    severity: str | None = None,
) -> dict:
    events = request.app.state.lifecycle.diagnostics.list_events(
        limit=limit, code=code, severity=severity
    )
    return {"count": len(events), "events": events}


@router.get("/events/{event_id}")
def get_event(event_id: str, request: Request) -> dict:
    ev = request.app.state.lifecycle.diagnostics.get_event(event_id)
    if ev is None:
        raise SessionError("event.not_found", f"event {event_id} not found", status=404)
    return ev


@router.get("/episodes")
def list_episodes(request: Request) -> dict:
    episodes = request.app.state.lifecycle.diagnostics.list_episodes()
    return {"count": len(episodes), "episodes": episodes}
