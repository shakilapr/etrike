"""Injection API endpoints (workplan §5.6)."""

from fastapi import APIRouter, Depends, HTTPException, Query

from vtc.api.models.injections import (
    CancelInjectionResponse,
    InjectionDetailResponse,
    InjectionInfo,
    InjectionListResponse,
    SubmitInjectionRequest,
    SubmitInjectionResponse,
)
from vtc.services.injections import InjectionService

# Router for injection endpoints
router = APIRouter(prefix="/api/v1/sessions", tags=["injections"])


# Dependency injection helper
def get_injection_service() -> InjectionService:
    """Get injection service from app state.

    In production, this would be injected from app.state or a service container.
    """
    raise HTTPException(status_code=500, detail="Injection service not configured")


@router.post(
    "/{session_id}/injections",
    response_model=SubmitInjectionResponse,
    summary="Submit a one-shot message injection",
)
async def submit_injection(
    session_id: str,
    body: SubmitInjectionRequest,
    service: InjectionService = Depends(get_injection_service),
) -> SubmitInjectionResponse:
    """Submit an injection for one-shot transmission.

    Args:
        session_id: Session ID
        body: Injection request with key, values, bus, optional delay
        service: Injection service

    Returns:
        Injection response with ID and status

    Raises:
        400: Invalid bus or encoding failure
        422: Validation error
    """
    # Validate bus
    if body.bus not in ("high", "low"):
        raise HTTPException(status_code=400, detail=f"Invalid bus: {body.bus}")

    # Submit injection
    record = await service.submit_injection(
        session_id=session_id,
        key=body.key,
        values=body.values,
        bus=body.bus,
        delay_ms=body.delay_ms,
        owner=session_id,
    )

    return SubmitInjectionResponse(
        injection_id=record.injection_id,
        status=record.status,
        can_id=record.can_id,
        error_code=record.error_code,
        error_message=record.error_message,
    )


@router.get(
    "/{session_id}/injections",
    response_model=InjectionListResponse,
    summary="List session injections",
)
async def list_injections(
    session_id: str,
    limit: int = Query(100, ge=1, le=1000),
    offset: int = Query(0, ge=0),
    status: str | None = Query(None, description="Filter by status"),
    service: InjectionService = Depends(get_injection_service),
) -> InjectionListResponse:
    """List injections for a session.

    Args:
        session_id: Session ID
        limit: Max results
        offset: Results offset
        status: Optional status filter (pending, submitted, failed, cancelled)
        service: Injection service

    Returns:
        List of injections with metadata
    """
    records, total = await service.list_injections(
        session_id=session_id,
        limit=limit,
        offset=offset,
        status_filter=status,
    )

    injections = [
        InjectionInfo(
            injection_id=r.injection_id,
            key=r.key,
            bus=r.bus,
            status=r.status,
            can_id=r.can_id,
            data=r.data,
            submitted_at=r.submitted_at,
            error_code=r.error_code,
            error_message=r.error_message,
            created_at=r.created_at,
        )
        for r in records
    ]

    return InjectionListResponse(total_count=total, injections=injections)


@router.get(
    "/{session_id}/injections/{injection_id}",
    response_model=InjectionDetailResponse,
    summary="Get injection details",
)
async def get_injection(
    session_id: str,
    injection_id: str,
    service: InjectionService = Depends(get_injection_service),
) -> InjectionDetailResponse:
    """Get details for a specific injection.

    Args:
        session_id: Session ID
        injection_id: Injection ID
        service: Injection service

    Returns:
        Injection details

    Raises:
        404: Injection not found
    """
    record = await service.get_injection(session_id, injection_id)
    if not record:
        raise HTTPException(status_code=404, detail="Injection not found")

    injection = InjectionInfo(
        injection_id=record.injection_id,
        key=record.key,
        bus=record.bus,
        status=record.status,
        can_id=record.can_id,
        data=record.data,
        submitted_at=record.submitted_at,
        error_code=record.error_code,
        error_message=record.error_message,
        created_at=record.created_at,
    )

    return InjectionDetailResponse(injection=injection)


@router.delete(
    "/{session_id}/injections/{injection_id}",
    response_model=CancelInjectionResponse,
    summary="Cancel a pending injection",
)
async def cancel_injection(
    session_id: str,
    injection_id: str,
    service: InjectionService = Depends(get_injection_service),
) -> CancelInjectionResponse:
    """Cancel a pending injection.

    Args:
        session_id: Session ID
        injection_id: Injection ID
        service: Injection service

    Returns:
        Cancellation confirmation

    Raises:
        404: Injection not found
        409: Injection cannot be cancelled (already submitted/failed)
    """
    # Get current record
    record = await service.get_injection(session_id, injection_id)
    if not record:
        raise HTTPException(status_code=404, detail="Injection not found")

    previous_status = record.status

    # Try to cancel
    cancelled = await service.cancel_injection(session_id, injection_id)

    if not cancelled:
        raise HTTPException(
            status_code=409,
            detail=f"Cannot cancel injection with status '{previous_status}'",
        )

    return CancelInjectionResponse(
        injection_id=injection_id,
        previous_status=previous_status,
    )


@router.get(
    "/{session_id}/injections-stats",
    summary="Get injection statistics",
)
async def get_injection_stats(
    session_id: str,
    service: InjectionService = Depends(get_injection_service),
) -> dict:
    """Get injection statistics for a session.

    Args:
        session_id: Session ID
        service: Injection service

    Returns:
        Stats including counts by status
    """
    stats = await service.get_session_stats(session_id)
    return {
        "session_id": session_id,
        "stats": stats,
    }
