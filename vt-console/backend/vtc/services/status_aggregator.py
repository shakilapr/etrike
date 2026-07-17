"""Status aggregator for real-time UI updates (workplan §5.7)."""

from __future__ import annotations

import asyncio
import time
from datetime import datetime
from typing import Any

from vtc.models.events import EventType, StateEvent
from vtc.services.injections import InjectionService
from vtc.services.source_conflict_monitor import SourceConflictMonitor
from vtc.services.synthetic_peers import SyntheticPeerEngine
from vtc.services.websocket_manager import WebSocketManager


class StatusAggregator:
    """Aggregates system status and streams updates to connected clients.

    Responsibilities:
    - Periodically gather status from all services
    - Emit status update events
    - Track UI-relevant metrics
    - Coordinate with WebSocket manager
    """

    def __init__(
        self,
        synthetic_peers: SyntheticPeerEngine,
        conflict_monitor: SourceConflictMonitor,
        injection_service: InjectionService,
        ws_manager: WebSocketManager,
    ):
        """Initialize status aggregator.

        Args:
            synthetic_peers: Synthetic peer engine
            conflict_monitor: Source conflict monitor
            injection_service: Injection service
            ws_manager: WebSocket manager
        """
        self.synthetic_peers = synthetic_peers
        self.conflict_monitor = conflict_monitor
        self.injection_service = injection_service
        self.ws_manager = ws_manager

        # Active aggregation tasks per session
        self.aggregation_tasks: dict[str, asyncio.Task] = {}

    async def start_aggregation(
        self, session_id: str, interval_ms: int = 1000
    ) -> None:
        """Start periodic status aggregation for a session.

        Args:
            session_id: Session ID
            interval_ms: Update interval in milliseconds
        """
        # Cancel existing task if any
        if session_id in self.aggregation_tasks:
            self.aggregation_tasks[session_id].cancel()

        # Start new task
        task = asyncio.create_task(
            self._aggregation_loop(session_id, interval_ms)
        )
        self.aggregation_tasks[session_id] = task

    async def stop_aggregation(self, session_id: str) -> None:
        """Stop periodic status aggregation for a session.

        Args:
            session_id: Session ID
        """
        if session_id in self.aggregation_tasks:
            task = self.aggregation_tasks.pop(session_id)
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass

    async def _aggregation_loop(self, session_id: str, interval_ms: int) -> None:
        """Background loop that periodically aggregates and broadcasts status.

        Args:
            session_id: Session ID
            interval_ms: Update interval in milliseconds
        """
        try:
            while True:
                # Gather current status
                status = await self._aggregate_status(session_id)

                # Emit status update event
                event = StateEvent(
                    type=EventType.STATUS_UPDATE,
                    session_id=session_id,
                    timestamp=datetime.utcnow(),
                    data=status,
                )
                await self.ws_manager.broadcast(session_id, event)

                # Wait for next update
                await asyncio.sleep(interval_ms / 1000.0)
        except asyncio.CancelledError:
            pass

    async def _aggregate_status(self, session_id: str) -> dict[str, Any]:
        """Gather current status from all services.

        Args:
            session_id: Session ID

        Returns:
            Status dict with all metrics
        """
        # Get synthetic peers status
        peers_status = await self.synthetic_peers.get_status()

        # Get conflict report
        conflict_report = await self.conflict_monitor.get_conflict_report()

        # Get injection stats
        injection_stats = await self.injection_service.get_session_stats(
            session_id
        )

        return {
            "synthetic_peers_active": peers_status["active_peer_count"],
            "listening": peers_status["listening"],
            "listen_remaining_ms": peers_status["listen_remaining_ms"],
            "pending_injections": injection_stats["pending"],
            "submitted_injections": injection_stats["submitted"],
            "failed_injections": injection_stats["failed"],
            "cancelled_injections": injection_stats["cancelled"],
            "total_injections": injection_stats["total"],
            "total_conflicts": conflict_report["conflict_count"],
            "timestamp_ns": time.monotonic_ns(),
        }

    async def emit_event(self, event: StateEvent) -> None:
        """Emit a custom event to all clients for a session.

        Args:
            event: Event to emit
        """
        await self.ws_manager.broadcast(event.session_id, event)
