"""Session REST API (Phase 3)."""

from __future__ import annotations

from fastapi import APIRouter, Request

from control_toolkit.models.session import (
    BenchTxRequest,
    ChangeProfileRequest,
    ClaimLeaseRequest,
    CloseSessionRequest,
    CreateSessionRequest,
    RenewLeaseRequest,
    StopAllRequest,
    VehicleViewRequest,
)
from control_toolkit.services.ownership import OwnershipConflict
from control_toolkit.services.session_manager import SessionError

router = APIRouter(prefix="/sessions", tags=["sessions"])


@router.get("")
def get_sessions(request: Request) -> dict:
    st = request.app.state.lifecycle.sessions.snapshot()
    return {"session": st.model_dump()}


@router.get("/profiles")
def list_profiles(request: Request) -> dict:
    """List operating profiles.

    Two transport modes share the same backend code:
    - **computer** (``pure_software``): dual virtual CAN buses on this PC
    - **real** (``bench_test`` / ``full_vehicle``): CANalyst-II CH0=High, CH1=Low
    """
    life = request.app.state.lifecycle
    discovery = life.physical_discovery()
    ok, reason = discovery.available, discovery.reason
    return {
        "transport_modes": [
            {
                "id": "computer",
                "label": "Computer (virtual)",
                "description": (
                    "Same Control Toolkit code on this PC. Dual virtual High/Low "
                    "buses — no USB adapter required."
                ),
                "destination": "virtual",
                "profile": "pure_software",
                "available": True,
                "adapter": "none",
            },
            {
                "id": "real",
                "label": "Real (CANalyst-II)",
                "description": (
                    "Physical vehicle/bench buses via CANalyst-II "
                    "(CH0 = High, CH1 = Low @ 500 kbit/s). Selectable without "
                    "adapter; link shows disconnected until USB is present."
                ),
                "destination": "physical",
                "profiles": ["bench_test", "full_vehicle"],
                "available": True,
                "link_available": ok,
                "reason": None if ok else reason,
                "adapter": "canalystii",
            },
        ],
        "profiles": [
            {
                "id": "pure_software",
                "label": "Computer · Virtual buses",
                "destination": "virtual",
                "mode": "computer",
                "available": True,
            },
            {
                "id": "bench_test",
                "label": "Real · Bench Test (CANalyst-II)",
                "destination": "physical",
                "mode": "real",
                "available": True,
                "link_available": ok,
                "reason": None if ok else reason,
            },
            {
                "id": "full_vehicle",
                "label": "Real · Full Vehicle (CANalyst-II)",
                "destination": "physical",
                "mode": "real",
                "available": True,
                "link_available": ok,
                "reason": None if ok else reason,
            },
        ],
        "physical_adapter": {
            **discovery.model_dump(),
            "kind": "canalystii",
            "available": ok,
            "reason": None if ok else reason,
            "channels": {"high": "CH0", "low": "CH1"},
        },
    }


@router.post("")
def create_session(request: Request, body: CreateSessionRequest) -> dict:
    life = request.app.state.lifecycle
    st = life.sessions.create(body)
    life.audit.log(
        category="session",
        code="session.created",
        title="Session created",
        detail=f"profile={st.profile} id={st.session_id}",
        session_id=st.session_id,
        data={"profile": str(st.profile), "destination": st.destination},
    )
    return {"session": st.model_dump()}


@router.post("/{session_id}/profile")
def change_profile(
    session_id: str, request: Request, body: ChangeProfileRequest
) -> dict:
    _check_id(request, session_id)
    st = request.app.state.lifecycle.sessions.change_profile(body)
    return {"session": st.model_dump()}


@router.post("/{session_id}/bench-tx")
def set_bench_tx(session_id: str, request: Request, body: BenchTxRequest) -> dict:
    _check_id(request, session_id)
    life = request.app.state.lifecycle
    st = life.sessions.set_bench_tx(body.enabled, body.expected_revision)
    life.audit.log(
        category="session",
        code="session.bench_tx",
        title="Bench TX " + ("enabled" if body.enabled else "disabled"),
        detail=f"session={session_id}",
        session_id=session_id,
        severity="warning" if body.enabled else "info",
        data={"enabled": body.enabled},
    )
    return {"session": st.model_dump()}


@router.post("/{session_id}/stop-all")
def stop_all(
    session_id: str, request: Request, body: StopAllRequest | None = None
) -> dict:
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
    outcome = body.outcome if body else None
    if outcome is None:
        from control_toolkit.models.session import SessionPhase

        outcome = SessionPhase.STOPPED
    st = request.app.state.lifecycle.sessions.close(rev, outcome)
    return {"session": st.model_dump()}


@router.post("/{session_id}/vehicle-view")
def set_vehicle_view(
    session_id: str, request: Request, body: VehicleViewRequest
) -> dict:
    """Update requested/observed vehicle state shown in the UI header."""
    _check_id(request, session_id)
    st = request.app.state.lifecycle.sessions.update_vehicle_view(
        requested_mode=body.requested_mode,
        confirmed_mode=body.confirmed_mode,
        requested_power=body.requested_power,
        confirmed_power=body.confirmed_power,
        estop_active=body.estop_active,
        recording=body.recording,
    )
    return {"session": st.model_dump()}


@router.post("/{session_id}/leases")
def claim_lease(session_id: str, request: Request, body: ClaimLeaseRequest) -> dict:
    _check_id(request, session_id)
    life = request.app.state.lifecycle
    try:
        lease = life.ownership.claim(
            bus=body.bus,
            can_id=body.can_id,
            owner=body.owner,
            resource=body.resource,
            ttl_s=body.ttl_s,
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
        raise SessionError(
            "lease.not_found", f"lease {body.lease_id} not found", status=404
        ) from exc
    return {"lease_id": lease.lease_id, "owner": lease.owner, "renewed": True}


@router.delete("/{session_id}/leases/{lease_id}")
def release_lease(session_id: str, lease_id: str, request: Request) -> dict:
    _check_id(request, session_id)
    request.app.state.lifecycle.ownership.release(lease_id)
    return {"lease_id": lease_id, "released": True}


def _check_id(request: Request, session_id: str) -> None:
    current = request.app.state.lifecycle.sessions.session_id()
    if current != session_id:
        raise SessionError(
            "session.not_found",
            f"session {session_id} is not active",
            status=404,
        )
