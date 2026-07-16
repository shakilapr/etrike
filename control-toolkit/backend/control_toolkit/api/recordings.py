"""Recording start/stop/list API (Phase 6)."""

from __future__ import annotations

from fastapi import APIRouter, Request

from control_toolkit import protocol_bridge as proto
from control_toolkit.services.session_manager import SessionError

router = APIRouter(prefix="/recordings", tags=["recordings"])


@router.get("")
def list_recordings(request: Request) -> dict:
    rec = request.app.state.lifecycle.recording
    active = rec.active()
    return {
        "active": active.to_summary() if active else None,
        "recordings": rec.list_recordings(),
    }


@router.post("")
def start_recording(request: Request) -> dict:
    life = request.app.state.lifecycle
    try:
        session = life.recording.start(wire_hash=proto.WIRE_HASH)
    except RuntimeError as exc:
        raise SessionError("recording.active", str(exc), status=409) from exc
    life.sessions.update_vehicle_view(recording=True)
    life.diagnostics.emit(
        code="recording.started",
        title="Recording started",
        detail=session.recording_id,
        severity="info",
        evidence={"recording_id": session.recording_id},
    )
    return {"recording": session.to_summary()}


@router.delete("/{recording_id}")
def stop_recording(recording_id: str, request: Request) -> dict:
    life = request.app.state.lifecycle
    session = life.recording.stop(recording_id)
    if session is None:
        raise SessionError(
            "recording.not_found",
            f"recording {recording_id} not active",
            status=404,
        )
    life.sessions.update_vehicle_view(recording=False)
    life.diagnostics.emit(
        code="recording.stopped",
        title="Recording stopped",
        detail=session.recording_id,
        severity="info",
        evidence={
            "recording_id": session.recording_id,
            "frame_count": len(session.frames),
            "evidence_quality": session.evidence_quality.value,
        },
    )
    return {"recording": session.to_summary()}


@router.get("/{recording_id}")
def get_recording(recording_id: str, request: Request) -> dict:
    body = request.app.state.lifecycle.recording.get(recording_id)
    if body is None:
        raise SessionError(
            "recording.not_found",
            f"recording {recording_id} not found",
            status=404,
        )
    return body
