"""RFC 9457 problem+json error responses (workplan §3.5)."""

from __future__ import annotations

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse

from vtc.services.session_manager import SessionError


def problem(*, status: int, code: str, title: str, detail: str) -> JSONResponse:
    body = {
        "type": f"about:blank#{code}",
        "title": title,
        "status": status,
        "detail": detail,
        "code": code,
    }
    return JSONResponse(status_code=status, content=body, media_type="application/problem+json")


def register_exception_handlers(app: FastAPI) -> None:
    @app.exception_handler(SessionError)
    async def _session_error(_request: Request, exc: SessionError) -> JSONResponse:
        return problem(status=exc.status, code=exc.code, title="Session error", detail=exc.detail)
