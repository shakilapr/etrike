"""Recording start/stop/list API (Phase 6)."""

from __future__ import annotations

import io
import json
import zipfile
from pathlib import Path

from fastapi import APIRouter, Request
from fastapi.responses import Response

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


@router.get("/{recording_id}/export")
def export_recording(recording_id: str, request: Request) -> dict:
    """JSON export of full recorded frame list + evidence quality."""
    body = request.app.state.lifecycle.recording.export_json(recording_id)
    if body is None:
        raise SessionError(
            "recording.not_found",
            f"recording {recording_id} not found",
            status=404,
        )
    return body


@router.get("/{recording_id}/export/vector")
def export_vector_bundle(recording_id: str, request: Request) -> Response:
    """Download BLF + High/Low DBC + sidecar for Vector CANalyzer."""
    exported = request.app.state.lifecycle.recording.export_blf(recording_id)
    if exported is None:
        raise SessionError(
            "recording.not_found",
            f"recording {recording_id} not found",
            status=404,
        )
    blf, sidecar = exported

    repo_root = Path(__file__).resolve().parents[4]
    dbc_dir = repo_root / "protocol" / "generated" / "dbc" / "buses"
    archive = io.BytesIO()
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
        bundle.writestr(f"{recording_id}.blf", blf)
        bundle.writestr(
            f"{recording_id}.metadata.json",
            json.dumps(sidecar, indent=2, sort_keys=True),
        )
        for bus in ("high", "low"):
            dbc = dbc_dir / f"{bus}.dbc"
            if dbc.is_file():
                bundle.writestr(f"dbc/etrike_{bus}.dbc", dbc.read_bytes())
        bundle.writestr(
            "README.txt",
            "Vector CANalyzer import bundle\n"
            "===============================\n"
            "Open the .blf file in CANalyzer. Assign dbc/etrike_high.dbc to "
            "channel 1 (Control Toolkit CH0/High) and dbc/etrike_low.dbc to "
            "channel 2 (Control Toolkit CH1/Low). The metadata JSON records "
            "clock, evidence quality, protocol hash, and limitations.\n",
        )

    filename = f"{recording_id}-canalyzer.zip"
    return Response(
        content=archive.getvalue(),
        media_type="application/zip",
        headers={"Content-Disposition": f'attachment; filename="{filename}"'},
    )
