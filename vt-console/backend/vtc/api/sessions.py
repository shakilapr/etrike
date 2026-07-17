"""Session REST API (workplan §3.5)."""

from __future__ import annotations

from fastapi import APIRouter, Request

from vtc.models.session import (
    BenchTxRequest,
    ChangeProfileRequest,
    ClaimLeaseRequest,
    CloseSessionRequest,
    CreateSessionRequest,
    RenewLeaseRequest,
    SessionPhase,
    StopAllRequest,
)
from vtc.services.ownership import OwnershipConflict
from vtc.services.session_manager import SessionError

router = APIRouter(prefix="/sessions", tags=["sessions"])

# Only Pure Software is reachable until Phase 2 (CANalyst-II transport) closes
# out — see the workplan's sequencing exception note.
_PROFILES = [
    {"id": "pure_software", "label": "Pure Software", "destination": "virtual", "available": True},
    {
        "id": "bench_test",
        "label": "Bench Test",
        "destination": "physical",
        "available": False,
        "reason": "physical adapter not available (Phase 2 deferred)",
    },
    {
        "id": "full_vehicle",
        "label": "Full Vehicle",
        "destination": "physical",
        "available": False,
        "reason": "physical adapter not available (Phase 2 deferred)",
    },
]


@router.get("")
def get_sessions(request: Request) -> dict:
    st = request.app.state.lifecycle.sessions.snapshot()
    return {"session": st.model_dump()}


@router.get("/profiles")
def list_profiles() -> dict:
    return {"profiles": _PROFILES}


@router.post("")
def create_session(request: Request, body: CreateSessionRequest) -> dict:
    st = request.app.state.lifecycle.sessions.create(body)
    return {"session": st.model_dump()}


@router.post("/{session_id}/profile")
def change_profile(session_id: str, request: Request, body: ChangeProfileRequest) -> dict:
    _check_id(request, session_id)
    st = request.app.state.lifecycle.sessions.change_profile(body)
    return {"session": st.model_dump()}


@router.post("/{session_id}/bench-tx")
def set_bench_tx(session_id: str, request: Request, body: BenchTxRequest) -> dict:
    _check_id(request, session_id)
    st = request.app.state.lifecycle.sessions.set_bench_tx(body.enabled, body.expected_revision)
    return {"session": st.model_dump()}


@router.post("/{session_id}/stop-all")
def stop_all(session_id: str, request: Request, body: StopAllRequest | None = None) -> dict:
    _check_id(request, session_id)
    rev = body.expected_revision if body else None
    st = request.app.state.lifecycle.sessions.stop_all(rev)
    return {"session": st.model_dump()}


@router.delete("/{session_id}")
def close_session(session_id: str, request: Request, body: CloseSessionRequest | None = None) -> dict:
    _check_id(request, session_id)
    rev = body.expected_revision if body else None
    outcome = body.outcome if body else SessionPhase.STOPPED
    st = request.app.state.lifecycle.sessions.close(rev, outcome)
    return {"session": st.model_dump()}


@router.post("/{session_id}/leases")
def claim_lease(session_id: str, request: Request, body: ClaimLeaseRequest) -> dict:
    _check_id(request, session_id)
    ownership = request.app.state.lifecycle.ownership
    try:
        lease = ownership.claim(
            bus=body.bus, can_id=body.can_id, owner=body.owner, resource=body.resource, ttl_s=body.ttl_s
        )
    except OwnershipConflict as exc:
        raise SessionError("ownership.conflict", str(exc), status=409) from exc
    return {
        "lease_id": lease.lease_id,
        "owner": lease.owner,
        "bus": lease.bus,
        "can_id": lease.can_id,
        "resource": lease.resource,
    }


@router.post("/{session_id}/leases/renew")
def renew_lease(session_id: str, request: Request, body: RenewLeaseRequest) -> dict:
    _check_id(request, session_id)
    try:
        lease = request.app.state.lifecycle.ownership.renew(body.lease_id, body.ttl_s)
    except KeyError as exc:
        raise SessionError("lease.not_found", f"lease {body.lease_id} not found", status=404) from exc
    return {"lease_id": lease.lease_id, "owner": lease.owner, "renewed": True}


@router.delete("/{session_id}/leases/{lease_id}")
def release_lease(session_id: str, lease_id: str, request: Request) -> dict:
    _check_id(request, session_id)
    request.app.state.lifecycle.ownership.release(lease_id)
    return {"lease_id": lease_id, "released": True}


def _check_id(request: Request, session_id: str) -> None:
    current = request.app.state.lifecycle.sessions.session_id()
    if current != session_id:
        raise SessionError("session.not_found", f"session {session_id} is not active", status=404)
