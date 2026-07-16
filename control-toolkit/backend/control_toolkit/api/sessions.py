"""Session REST API."""

from __future__ import annotations

from fastapi import APIRouter, Request

from control_toolkit.models.session import (
    BenchTxRequest,
    CloseSessionRequest,
    CreateSessionRequest,
    StopAllRequest,
)

router = APIRouter(prefix="/sessions", tags=["sessions"])


@router.get("")
def get_sessions(request: Request) -> dict:
    st = request.app.state.lifecycle.sessions.snapshot()
    return {"session": st.model_dump()}


@router.post("")
def create_session(request: Request, body: CreateSessionRequest) -> dict:
    st = request.app.state.lifecycle.sessions.create(body)
    return {"session": st.model_dump()}


@router.post("/{session_id}/bench-tx")
def set_bench_tx(session_id: str, request: Request, body: BenchTxRequest) -> dict:
    _check_id(request, session_id)
    st = request.app.state.lifecycle.sessions.set_bench_tx(
        body.enabled, body.expected_revision
    )
    return {"session": st.model_dump()}


@router.post("/{session_id}/stop-all")
def stop_all(session_id: str, request: Request, body: StopAllRequest | None = None) -> dict:
    _check_id(request, session_id)
    rev = body.expected_revision if body else None
    st = request.app.state.lifecycle.sessions.stop_all(rev)
    return {"session": st.model_dump()}


@router.delete("/{session_id}")
def close_session(
    session_id: str, request: Request, body: CloseSessionRequest | None = None
) -> dict:
    _check_id(request, session_id)
    rev = body.expected_revision if body else None
    st = request.app.state.lifecycle.sessions.close(rev)
    return {"session": st.model_dump()}


def _check_id(request: Request, session_id: str) -> None:
    from control_toolkit.services.session_manager import SessionError

    current = request.app.state.lifecycle.sessions.session_id()
    if current != session_id:
        raise SessionError(
            "session.not_found",
            f"session {session_id} is not active",
            status=404,
        )
