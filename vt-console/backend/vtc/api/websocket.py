"""WebSocket endpoint for real-time session updates (workplan §5.7)."""

from fastapi import APIRouter, Depends, WebSocket, WebSocketDisconnect, HTTPException

from vtc.services.hmi import HmiService
from vtc.services.status_aggregator import StatusAggregator
from vtc.services.websocket_manager import WebSocketManager

router = APIRouter()


def get_ws_manager() -> WebSocketManager:
    """Get WebSocket manager from app state."""
    raise HTTPException(status_code=500, detail="WebSocket manager not configured")


def get_status_aggregator() -> StatusAggregator:
    """Get status aggregator from app state."""
    raise HTTPException(status_code=500, detail="Status aggregator not configured")


def get_hmi_service() -> HmiService:
    """Get HMI service from app state."""
    raise HTTPException(status_code=500, detail="HMI service not configured")


@router.websocket("/api/v1/sessions/{session_id}/ws")
async def websocket_endpoint(
    websocket: WebSocket,
    session_id: str,
    hmi_service: HmiService = Depends(get_hmi_service),
    ws_manager: WebSocketManager = Depends(get_ws_manager),
    aggregator: StatusAggregator = Depends(get_status_aggregator),
) -> None:
    """WebSocket endpoint for real-time session updates.

    Provides real-time streaming of:
    - Synthetic peer activation/deactivation
    - Injection submission/completion
    - Conflict detection
    - System status updates (every 1s)
    - Bench test state changes

    Example client usage (JavaScript):
    ```javascript
    const ws = new WebSocket("ws://localhost:8000/api/v1/sessions/ses_123/ws");

    ws.onmessage = (event) => {
        const message = JSON.parse(event.data);
        console.log("Event:", message.type, message.data);

        if (message.type === "status.update") {
            // Update UI with latest metrics
            updateStatusDisplay(message.data);
        }
    };

    ws.onerror = (error) => {
        console.error("WebSocket error:", error);
    };
    ```

    Args:
        websocket: WebSocket connection
        session_id: Session ID
        hmi_service: HMI service for validation
        ws_manager: WebSocket manager for connection tracking
        aggregator: Status aggregator for periodic updates

    Raises:
        403: Session not found
    """
    # Verify session exists
    try:
        session = await hmi_service.get_session(session_id)
        if not session:
            await websocket.close(code=4003, reason="Session not found")
            return
    except Exception:
        await websocket.close(code=4500, reason="Session validation failed")
        return

    # Accept connection
    await websocket.accept()

    # Register connection
    await ws_manager.connect(session_id, websocket)

    # Start status aggregation if not already running
    await aggregator.start_aggregation(session_id, interval_ms=1000)

    try:
        # Keep connection alive
        while True:
            # Wait for messages (client can send ping/keep-alive)
            data = await websocket.receive_text()
            # In a full implementation, you could handle client commands here
            # For now, we just keep the connection open
    except WebSocketDisconnect:
        # Clean up connection
        await ws_manager.disconnect(session_id, websocket)

        # Check if other clients are still connected
        remaining = await ws_manager.get_connection_count(session_id)
        if remaining == 0:
            # No more clients, stop aggregation
            await aggregator.stop_aggregation(session_id)
    except Exception:
        # Unexpected error
        await ws_manager.disconnect(session_id, websocket)
        await aggregator.stop_aggregation(session_id)
