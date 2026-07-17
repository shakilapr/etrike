"""Synthetic Peers API endpoints (workplan §5.4 - §5.5 integration)."""

from fastapi import APIRouter, Depends, HTTPException, Query
from pydantic import ValidationError

from vtc.api.models.synthetic_peers import (
    ActivatePeersRequest,
    ActivatePeersResponse,
    ActivePeerInstance,
    AvailablePeersResponse,
    ConflictDetail,
    ConflictReportResponse,
    ListenWindowRequest,
    ListenWindowResponse,
    PeerInfo,
    PeerStatusDetail,
    StopPeersResponse,
    SyntheticPeersStatusResponse,
)
from vtc.services.source_conflict_monitor import SourceConflictMonitor
from vtc.services.synthetic_peers import DutKind, SyntheticPeerEngine

# Router for synthetic peers endpoints
router = APIRouter(prefix="/api/v1/sessions", tags=["synthetic-peers"])


# Dependency injection helpers (would be configured in app startup)
def get_synthetic_engine() -> SyntheticPeerEngine:
    """Get synthetic peer engine from app state.

    In production, this would be injected from app.state or a service container.
    """
    # This is a placeholder - will be properly wired up when integrated with main app
    raise HTTPException(status_code=500, detail="Synthetic peer engine not configured")


def get_conflict_monitor() -> SourceConflictMonitor:
    """Get source conflict monitor from app state."""
    raise HTTPException(status_code=500, detail="Conflict monitor not configured")


@router.get(
    "/{session_id}/synthetic-peers/available",
    response_model=AvailablePeersResponse,
    summary="List available synthetic peer templates",
)
async def list_available_peers(
    session_id: str,
    dut: str = Query(..., description="DUT kind: 'sys' or 'rt'"),
    engine: SyntheticPeerEngine = Depends(get_synthetic_engine),
) -> AvailablePeersResponse:
    """List available synthetic peer templates for a DUT.

    Args:
        session_id: Session ID
        dut: Device-under-test ('sys' or 'rt')
        engine: Synthetic peer engine

    Returns:
        Available peers for the DUT

    Raises:
        400: Invalid DUT kind
        500: Engine not available
    """
    try:
        dut_kind = DutKind(dut)
    except ValueError:
        raise HTTPException(status_code=400, detail=f"Invalid DUT kind: {dut}")

    # Get available peers from engine
    peers_list = await engine.list_available_peers(dut_kind)

    # Convert to API models
    peers = [
        PeerInfo(
            name=p["name"],
            key=p["key"],
            bus=p["bus"],
            period_ms=p["period_ms"],
            has_counter=p["has_counter"],
        )
        for p in peers_list
    ]

    return AvailablePeersResponse(dut=dut, peer_count=len(peers), peers=peers)


@router.post(
    "/{session_id}/synthetic-peers/listen",
    response_model=ListenWindowResponse,
    summary="Start listen-before-speak window",
)
async def start_listen_window(
    session_id: str,
    body: ListenWindowRequest,
    engine: SyntheticPeerEngine = Depends(get_synthetic_engine),
    monitor: SourceConflictMonitor = Depends(get_conflict_monitor),
) -> ListenWindowResponse:
    """Start a listen-before-speak window to detect existing physical ECU traffic.

    Args:
        session_id: Session ID
        body: Request with DUT and duration
        engine: Synthetic peer engine
        monitor: Conflict monitor

    Returns:
        Listen window status

    Raises:
        400: Invalid DUT kind
        409: Already listening
    """
    try:
        dut_kind = DutKind(body.dut)
    except ValueError:
        raise HTTPException(status_code=400, detail=f"Invalid DUT kind: {body.dut}")

    # Check if already listening
    if engine.listen_window and engine.listen_window.is_active(
        __import__("time").monotonic_ns()
    ):
        raise HTTPException(status_code=409, detail="Already listening")

    # Start listen window
    window = await engine.start_listen_window(
        session_id=session_id,
        dut=dut_kind,
        duration_ms=body.duration_ms,
    )

    # Also start monitoring
    await monitor.start_monitoring(session_id=session_id, dut=dut_kind)

    return ListenWindowResponse(
        listening=True,
        dut=body.dut,
        duration_ms=body.duration_ms,
        remaining_ms=window.remaining_ms(__import__("time").monotonic_ns()),
    )


@router.post(
    "/{session_id}/synthetic-peers/activate",
    response_model=ActivatePeersResponse,
    summary="Activate synthetic peer set",
)
async def activate_peers(
    session_id: str,
    body: ActivatePeersRequest,
    engine: SyntheticPeerEngine = Depends(get_synthetic_engine),
) -> ActivatePeersResponse:
    """Activate synthetic peers for a DUT after listen window.

    Args:
        session_id: Session ID
        body: Request with DUT
        engine: Synthetic peer engine

    Returns:
        Activated peer instances

    Raises:
        400: Invalid DUT kind
        409: Peers already active
    """
    try:
        dut_kind = DutKind(body.dut)
    except ValueError:
        raise HTTPException(status_code=400, detail=f"Invalid DUT kind: {body.dut}")

    # Check if already active
    if engine.active_peers and engine.current_dut == dut_kind:
        raise HTTPException(
            status_code=409, detail=f"Synthetic peers already active for {body.dut}"
        )

    # Activate peers
    activated = await engine.activate_peers(
        session_id=session_id,
        dut=dut_kind,
    )

    # Convert to API models
    peers = [
        ActivePeerInstance(
            name=name,
            job_id=instance.job_id,
            key=instance.peer_template.key,
            bus=instance.peer_template.bus,
            period_ms=instance.peer_template.period_ms,
            submissions=instance.submission_count,
        )
        for name, instance in activated.items()
    ]

    return ActivatePeersResponse(activated_count=len(peers), peers=peers)


@router.get(
    "/{session_id}/synthetic-peers/status",
    response_model=SyntheticPeersStatusResponse,
    summary="Get synthetic peers status",
)
async def get_status(
    session_id: str,
    engine: SyntheticPeerEngine = Depends(get_synthetic_engine),
    monitor: SourceConflictMonitor = Depends(get_conflict_monitor),
) -> SyntheticPeersStatusResponse:
    """Get current status of synthetic peers and listen window.

    Args:
        session_id: Session ID
        engine: Synthetic peer engine
        monitor: Conflict monitor

    Returns:
        Full status including active peers and conflicts
    """
    import time

    now_ns = time.monotonic_ns()

    # Get engine status
    engine_status = await engine.get_status()
    monitor_report = await monitor.get_conflict_report()

    # Convert active peers
    active_peers = [
        PeerStatusDetail(
            name=p["name"],
            key=p["key"],
            bus=p["bus"],
            period_ms=p["period_ms"],
            submissions=p["submissions"],
        )
        for p in engine_status["active_peers"]
    ]

    # Convert conflicts
    conflicts = [
        ConflictDetail(
            timestamp_ns=c["timestamp_ns"],
            bus=c["bus"],
            can_id=c["can_id"],
            peer=c["peer"],
            source=c["source"],
        )
        for c in monitor_report["recent_conflicts"]
    ]

    return SyntheticPeersStatusResponse(
        dut=engine_status["dut"],
        listening=engine_status["listening"],
        listen_remaining_ms=engine_status["listen_remaining_ms"],
        active_peer_count=engine_status["active_peer_count"],
        active_peers=active_peers,
        conflicts=conflicts,
    )


@router.post(
    "/{session_id}/synthetic-peers/stop",
    response_model=StopPeersResponse,
    summary="Stop all synthetic peers",
)
async def stop_all_peers(
    session_id: str,
    engine: SyntheticPeerEngine = Depends(get_synthetic_engine),
    monitor: SourceConflictMonitor = Depends(get_conflict_monitor),
) -> StopPeersResponse:
    """Stop all active synthetic peers.

    Args:
        session_id: Session ID
        engine: Synthetic peer engine
        monitor: Conflict monitor

    Returns:
        Number of peers stopped
    """
    # Stop all peers
    stopped_count = await engine.stop_all(session_id)

    # Stop monitoring
    await monitor.stop_monitoring(session_id)

    return StopPeersResponse(
        stopped_count=stopped_count,
        status="all_peers_stopped" if stopped_count > 0 else "no_peers_active",
    )


@router.get(
    "/{session_id}/synthetic-peers/conflicts",
    response_model=ConflictReportResponse,
    summary="Get source conflict report",
)
async def get_conflict_report(
    session_id: str,
    monitor: SourceConflictMonitor = Depends(get_conflict_monitor),
) -> ConflictReportResponse:
    """Get conflict detection report.

    Args:
        session_id: Session ID
        monitor: Conflict monitor

    Returns:
        Conflict history and current conflicts
    """
    report = await monitor.get_conflict_report()

    # Convert conflicts
    conflicts = [
        ConflictDetail(
            timestamp_ns=c["timestamp_ns"],
            bus=c["bus"],
            can_id=c["can_id"],
            peer=c["peer"],
            source=c["source"],
        )
        for c in report["recent_conflicts"]
    ]

    return ConflictReportResponse(
        conflict_count=report["conflict_count"],
        recent_conflicts=conflicts,
    )
