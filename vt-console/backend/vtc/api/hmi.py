"""HMI API endpoints (workplan §5.7)."""

from fastapi import APIRouter, Depends, HTTPException

from vtc.api.models.hmi import (
    CreateSessionRequest,
    CreateSessionResponse,
    DeleteSessionResponse,
    SessionDetailResponse,
    SessionInfo,
    SessionListResponse,
    StartBenchTestRequest,
    StartBenchTestResponse,
    StopBenchTestResponse,
    SystemStatusResponse,
    UpdateSessionRequest,
    UpdateSessionResponse,
)
from vtc.services.hmi import HmiService

# Router for HMI endpoints
router = APIRouter(prefix="/api/v1", tags=["hmi"])


# Dependency injection helper
def get_hmi_service() -> HmiService:
    """Get HMI service from app state.

    In production, this would be injected from app.state or a service container.
    """
    raise HTTPException(status_code=500, detail="HMI service not configured")


@router.post(
    "/sessions",
    response_model=CreateSessionResponse,
    summary="Create a new session",
)
async def create_session(
    body: CreateSessionRequest,
    service: HmiService = Depends(get_hmi_service),
) -> CreateSessionResponse:
    """Create a new HMI session.

    Args:
        body: Session creation request
        service: HMI service

    Returns:
        Created session info

    Raises:
        400: Invalid profile or DUT
    """
    try:
        session = await service.create_session(
            name=body.name,
            profile=body.profile,
            dut=body.dut,
        )

        return CreateSessionResponse(
            session_id=session.session_id,
            name=session.name,
            profile=session.profile,
            dut=session.dut,
            created_at=session.created_at,
        )
    except ValueError as e:
        raise HTTPException(status_code=400, detail=str(e))


@router.get(
    "/sessions",
    response_model=SessionListResponse,
    summary="List all sessions",
)
async def list_sessions(
    service: HmiService = Depends(get_hmi_service),
) -> SessionListResponse:
    """List all HMI sessions.

    Returns:
        List of sessions
    """
    sessions = await service.list_sessions()

    session_infos = [
        SessionInfo(
            session_id=s.session_id,
            name=s.name,
            profile=s.profile,
            dut=s.dut,
            bench_tx_enabled=s.bench_tx_enabled,
            created_at=s.created_at,
            updated_at=s.updated_at,
        )
        for s in sessions
    ]

    return SessionListResponse(
        total_count=len(session_infos),
        sessions=session_infos,
    )


@router.get(
    "/sessions/{session_id}",
    response_model=SessionDetailResponse,
    summary="Get session details",
)
async def get_session(
    session_id: str,
    service: HmiService = Depends(get_hmi_service),
) -> SessionDetailResponse:
    """Get details for a specific session.

    Args:
        session_id: Session ID
        service: HMI service

    Returns:
        Session details

    Raises:
        404: Session not found
    """
    session = await service.get_session(session_id)
    if not session:
        raise HTTPException(status_code=404, detail="Session not found")

    session_info = SessionInfo(
        session_id=session.session_id,
        name=session.name,
        profile=session.profile,
        dut=session.dut,
        bench_tx_enabled=session.bench_tx_enabled,
        created_at=session.created_at,
        updated_at=session.updated_at,
    )

    return SessionDetailResponse(session=session_info)


@router.put(
    "/sessions/{session_id}",
    response_model=UpdateSessionResponse,
    summary="Update session settings",
)
async def update_session(
    session_id: str,
    body: UpdateSessionRequest,
    service: HmiService = Depends(get_hmi_service),
) -> UpdateSessionResponse:
    """Update session settings.

    Args:
        session_id: Session ID
        body: Update request
        service: HMI service

    Returns:
        Updated session info

    Raises:
        400: Invalid settings
        404: Session not found
    """
    try:
        session = await service.update_session(
            session_id,
            profile=body.profile,
            bench_tx_enabled=body.bench_tx_enabled,
        )

        if not session:
            raise HTTPException(status_code=404, detail="Session not found")

        return UpdateSessionResponse(
            session_id=session_id,
            profile=body.profile,
            bench_tx_enabled=body.bench_tx_enabled,
        )
    except ValueError as e:
        raise HTTPException(status_code=400, detail=str(e))


@router.delete(
    "/sessions/{session_id}",
    response_model=DeleteSessionResponse,
    summary="Delete session",
)
async def delete_session(
    session_id: str,
    service: HmiService = Depends(get_hmi_service),
) -> DeleteSessionResponse:
    """Delete a session and clean up all resources.

    Args:
        session_id: Session ID
        service: HMI service

    Returns:
        Cleanup confirmation

    Raises:
        404: Session not found
    """
    # Verify session exists
    session = await service.get_session(session_id)
    if not session:
        raise HTTPException(status_code=404, detail="Session not found")

    # Delete
    cleanup = await service.delete_session(session_id)

    return DeleteSessionResponse(
        session_id=session_id,
        status="deleted",
        cleaned_up_peers=cleanup["peers"],
        cleaned_up_injections=cleanup["injections"],
    )


@router.post(
    "/sessions/{session_id}/bench-test/start",
    response_model=StartBenchTestResponse,
    summary="Start bench testing",
)
async def start_bench_test(
    session_id: str,
    body: StartBenchTestRequest,
    service: HmiService = Depends(get_hmi_service),
) -> StartBenchTestResponse:
    """Start bench testing workflow.

    Args:
        session_id: Session ID
        body: Start request with DUT and listen duration
        service: HMI service

    Returns:
        Bench test status

    Raises:
        400: Invalid DUT
        404: Session not found
    """
    try:
        result = await service.start_bench_test(
            session_id,
            dut=body.dut,
            listen_duration_ms=body.listen_duration_ms,
        )

        return StartBenchTestResponse(
            session_id=session_id,
            status=result["status"],
            listening=result["listening"],
            listening_remaining_ms=result["listening_remaining_ms"],
        )
    except ValueError as e:
        raise HTTPException(status_code=400, detail=str(e))


@router.post(
    "/sessions/{session_id}/bench-test/stop",
    response_model=StopBenchTestResponse,
    summary="Stop bench testing",
)
async def stop_bench_test(
    session_id: str,
    service: HmiService = Depends(get_hmi_service),
) -> StopBenchTestResponse:
    """Stop bench testing and clean up.

    Args:
        session_id: Session ID
        service: HMI service

    Returns:
        Stop confirmation with cleanup counts

    Raises:
        404: Session not found
    """
    # Verify session exists
    session = await service.get_session(session_id)
    if not session:
        raise HTTPException(status_code=404, detail="Session not found")

    result = await service.stop_bench_test(session_id)

    return StopBenchTestResponse(
        session_id=session_id,
        status=result["status"],
        synthetic_peers_stopped=result["synthetic_peers_stopped"],
        pending_injections_cancelled=result["pending_injections_cancelled"],
    )


@router.get(
    "/sessions/{session_id}/status",
    response_model=SystemStatusResponse,
    summary="Get system status",
)
async def get_system_status(
    session_id: str,
    service: HmiService = Depends(get_hmi_service),
) -> SystemStatusResponse:
    """Get overall system status for a session.

    Args:
        session_id: Session ID
        service: HMI service

    Returns:
        System status

    Raises:
        404: Session not found
    """
    try:
        status = await service.get_system_status(session_id)

        return SystemStatusResponse(
            session_id=status["session_id"],
            profile=status["profile"],
            dut=status["dut"],
            bench_tx_enabled=status["bench_tx_enabled"],
            synthetic_peers_active=status["synthetic_peers_active"],
            pending_injections=status["pending_injections"],
            submitted_injections=status["submitted_injections"],
            total_conflicts=status["total_conflicts"],
            listening=status["listening"],
        )
    except ValueError as e:
        raise HTTPException(status_code=404, detail=str(e))


@router.get(
    "/sessions/{session_id}/control-workspace-status",
    summary="Get control workspace status",
)
async def get_control_workspace_status(
    session_id: str,
    service: HmiService = Depends(get_hmi_service),
) -> dict:
    """Get status optimized for control workspace UI.

    Args:
        session_id: Session ID
        service: HMI service

    Returns:
        Control workspace status

    Raises:
        404: Session not found
    """
    try:
        return await service.get_control_workspace_status(session_id)
    except ValueError as e:
        raise HTTPException(status_code=404, detail=str(e))
