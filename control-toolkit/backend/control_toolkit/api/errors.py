"""RFC 9457 problem+json helpers and exception handlers (Phase 9 min)."""

from __future__ import annotations

import logging

from fastapi import FastAPI, Request
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse
from starlette.exceptions import HTTPException as StarletteHTTPException

from control_toolkit.services.session_manager import SessionError

log = logging.getLogger("control_toolkit.api")


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
    return JSONResponse(
        status_code=status, content=body, media_type="application/problem+json"
    )


def register_exception_handlers(app: FastAPI) -> None:
    @app.exception_handler(SessionError)
    async def _session_error(request: Request, exc: SessionError) -> JSONResponse:
        log.info(
            "api_error code=%s status=%s path=%s detail=%s",
            exc.code,
            exc.status,
            request.url.path,
            exc.detail,
        )
        return problem(
            status=exc.status,
            code=exc.code,
            title="Request rejected",
            detail=exc.detail,
        )

    @app.exception_handler(RequestValidationError)
    async def _validation_error(
        request: Request, exc: RequestValidationError
    ) -> JSONResponse:
        log.info("api_validation path=%s errors=%s", request.url.path, exc.errors())
        return problem(
            status=422,
            code="request.validation_failed",
            title="Validation failed",
            detail="One or more request fields are invalid",
            extra={"errors": exc.errors()},
        )

    @app.exception_handler(StarletteHTTPException)
    async def _http_error(request: Request, exc: StarletteHTTPException) -> JSONResponse:
        code = f"http.{exc.status_code}"
        detail = exc.detail if isinstance(exc.detail, str) else str(exc.detail)
        return problem(
            status=exc.status_code,
            code=code,
            title="HTTP error",
            detail=detail,
        )

    @app.exception_handler(Exception)
    async def _unhandled(request: Request, exc: Exception) -> JSONResponse:
        log.exception("unhandled path=%s", request.url.path)
        return problem(
            status=500,
            code="internal.error",
            title="Internal error",
            detail="An unexpected error occurred",
        )
