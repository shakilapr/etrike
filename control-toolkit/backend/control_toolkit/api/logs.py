"""Operational audit log API (Logging workspace)."""

import json
import time
from pathlib import Path

from fastapi import APIRouter, Query, Request

from control_toolkit.services.session_manager import SessionError

router = APIRouter(prefix="/logs", tags=["logs"])


@router.post("/export-save")
async def save_export(request: Request) -> dict:
    """Save an exported diagnostic bundle or log file into tem/control-toolkit-logs (gitignored)."""
    try:
        body = await request.json()
    except Exception:
        body = {}

    repo_root = Path(__file__).resolve().parents[4]
    tem_logs_dir = repo_root / "tem" / "control-toolkit-logs"
    tem_logs_dir.mkdir(parents=True, exist_ok=True)

    filename = request.query_params.get("filename")
    if not filename and isinstance(body, dict):
        filename = body.get("filename")
    if not filename:
        ts = int(time.time() * 1000)
        filename = f"control-toolkit-export-{ts}.json"

    clean_name = Path(filename).name
    if not clean_name.endswith(".json"):
        clean_name += ".json"

    data = (
        body.get("data", body)
        if (isinstance(body, dict) and "data" in body and "export_metadata" not in body)
        else body
    )

    target_path = tem_logs_dir / clean_name
    target_path.write_text(json.dumps(data, indent=2, default=str), encoding="utf-8")

    return {
        "ok": True,
        "saved_path": str(target_path),
        "filename": clean_name,
        "size_bytes": target_path.stat().st_size,
    }


@router.get("/saved-exports")
def list_saved_exports() -> dict:
    """List all telemetry and log files saved under tem/control-toolkit-logs."""
    repo_root = Path(__file__).resolve().parents[4]
    tem_logs_dir = repo_root / "tem" / "control-toolkit-logs"
    if not tem_logs_dir.exists():
        return {"count": 0, "exports": []}
    files = []
    for f in sorted(
        tem_logs_dir.glob("*.json"), key=lambda p: p.stat().st_mtime, reverse=True
    ):
        files.append(
            {
                "filename": f.name,
                "size_bytes": f.stat().st_size,
                "modified_ts": f.stat().st_mtime,
            }
        )
    return {"count": len(files), "exports": files}



@router.get("")
def list_logs(
    request: Request,
    limit: int = Query(default=200, ge=1, le=5000),
    category: str | None = None,
    severity: str | None = None,
    code: str | None = None,
    bus: str | None = None,
    can_id: str | None = None,
    q: str | None = None,
) -> dict:
    parsed_can_id: int | None = None
    if can_id is not None and can_id.strip() != "":
        try:
            parsed_can_id = int(can_id.strip(), 0)
        except ValueError:
            pass
    audit = request.app.state.lifecycle.audit
    entries = audit.list_logs(
        limit=limit,
        category=category,
        severity=severity,
        code=code,
        bus=bus,
        can_id=parsed_can_id,
        q=q,
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
