"""RFC 9457 problem+json helpers and exception handlers."""

from __future__ import annotations

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse

from control_toolkit.services.session_manager import SessionError


def problem(
    *,
    status: int,
    code: str,
    title: str,
    detail: str,
    request_id: str | None = None,
    extra: dict | None = None,
) -> JSONResponse:
    body = {
        "type": f"about:blank#{code}",
        "title": title,
        "status": status,
        "detail": detail,
        "code": code,
        "catalog_id": code.upper().replace(".", "-"),
    }
    if request_id:
        body["request_id"] = request_id
    if extra:
        body.update(extra)
    return JSONResponse(status_code=status, content=body, media_type="application/problem+json")


def register_exception_handlers(app: FastAPI) -> None:
    @app.exception_handler(SessionError)
    async def _session_error(_request: Request, exc: SessionError) -> JSONResponse:
        return problem(
            status=exc.status,
            code=exc.code,
            title="Session error",
            detail=exc.detail,
        )
