"""Evidence window fetch (Phase 6).

Evidence IDs are recording IDs today; the window returns bounded raw frames
plus quality metadata for linking from diagnostics / tests.
"""

from __future__ import annotations

from fastapi import APIRouter, Query, Request

from control_toolkit.services.session_manager import SessionError

router = APIRouter(prefix="/evidence", tags=["evidence"])


@router.get("/{evidence_id}")
def get_evidence(
    evidence_id: str,
    request: Request,
    limit: int = Query(default=200, ge=1, le=5000),
    offset: int = Query(default=0, ge=0),
) -> dict:
    """Fetch an evidence window. ``evidence_id`` is a recording_id."""
    rec = request.app.state.lifecycle.recording
    detail = rec.get_window(evidence_id, offset=offset, limit=limit)
    if detail is None:
        raise SessionError(
            "evidence.not_found",
            f"evidence {evidence_id} not found",
            status=404,
        )
    return {
        "evidence_id": evidence_id,
        "kind": "recording",
        **detail,
    }
