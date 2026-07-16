"""Operational audit log API (Logging workspace)."""

from __future__ import annotations

from fastapi import APIRouter, Query, Request

from control_toolkit.services.session_manager import SessionError

router = APIRouter(prefix="/logs", tags=["logs"])


@router.get("")
def list_logs(
    request: Request,
    limit: int = Query(default=200, ge=1, le=5000),
    category: str | None = None,
    severity: str | None = None,
    code: str | None = None,
    q: str | None = None,
) -> dict:
    audit = request.app.state.lifecycle.audit
    entries = audit.list_logs(
        limit=limit, category=category, severity=severity, code=code, q=q
    )
    return {
        "count": len(entries),
        "stats": audit.stats(),
        "logs": entries,
    }


@router.get("/stats")
def log_stats(request: Request) -> dict:
    return request.app.state.lifecycle.audit.stats()


@router.delete("")
def clear_logs(request: Request) -> dict:
    n = request.app.state.lifecycle.audit.clear()
    request.app.state.lifecycle.audit.log(
        category="system",
        code="log.cleared",
        title="Audit log cleared",
        detail=f"removed {n} entries",
        severity="warning",
    )
    return {"cleared": n}


@router.get("/{log_id}")
def get_log(log_id: str, request: Request) -> dict:
    entries = request.app.state.lifecycle.audit.list_logs(limit=5000)
    for e in entries:
        if e["log_id"] == log_id:
            return e
    raise SessionError("log.not_found", f"log {log_id} not found", status=404)
