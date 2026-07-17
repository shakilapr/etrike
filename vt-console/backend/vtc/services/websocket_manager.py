"""WebSocket connection manager for real-time event streaming (workplan §5.7)."""

from __future__ import annotations

import asyncio
import json
from typing import Any

from vtc.models.events import StateEvent


class WebSocketManager:
    """Manages WebSocket connections and broadcasts state events.

    Responsibilities:
    - Track active WebSocket connections per session
    - Broadcast events to all clients in a session
    - Handle connection lifecycle (connect/disconnect)
    - Serialize events to JSON
    """

    def __init__(self):
        """Initialize WebSocket manager."""
        # session_id → set of WebSocket connections
        self.active_connections: dict[str, set[Any]] = {}
        self._lock = asyncio.Lock()

    async def connect(self, session_id: str, websocket: Any) -> None:
        """Register a WebSocket connection for a session.

        Args:
            session_id: Session ID
            websocket: WebSocket connection object
        """
        async with self._lock:
            if session_id not in self.active_connections:
                self.active_connections[session_id] = set()
            self.active_connections[session_id].add(websocket)

    async def disconnect(self, session_id: str, websocket: Any) -> None:
        """Unregister a WebSocket connection.

        Args:
            session_id: Session ID
            websocket: WebSocket connection object
        """
        async with self._lock:
            if session_id in self.active_connections:
                self.active_connections[session_id].discard(websocket)
                if not self.active_connections[session_id]:
                    del self.active_connections[session_id]

    async def broadcast(self, session_id: str, event: StateEvent) -> None:
        """Broadcast an event to all connections for a session.

        Args:
            session_id: Session ID
            event: Event to broadcast
        """
        async with self._lock:
            if session_id not in self.active_connections:
                return

            # Get snapshot of connections to avoid holding lock during send
            connections = list(self.active_connections[session_id])

        # Send to all connections (outside lock)
        for websocket in connections:
            try:
                await self.send_personal(websocket, event)
            except Exception:
                # Connection may have closed, will be cleaned up on next disconnect
                pass

    async def send_personal(self, websocket: Any, event: StateEvent) -> None:
        """Send event to a specific connection.

        Args:
            websocket: WebSocket connection
            event: Event to send
        """
        message = json.dumps(event.to_dict())
        await websocket.send_text(message)

    async def get_connection_count(self, session_id: str) -> int:
        """Get number of active connections for a session.

        Args:
            session_id: Session ID

        Returns:
            Number of active connections
        """
        async with self._lock:
            return len(self.active_connections.get(session_id, set()))
