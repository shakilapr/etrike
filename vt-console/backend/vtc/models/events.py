"""Real-time event models for WebSocket streaming (workplan §5.7)."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime
from enum import Enum


class EventType(str, Enum):
    """Types of real-time events for UI streaming."""

    SESSION_CREATED = "session.created"
    SESSION_UPDATED = "session.updated"
    SESSION_DELETED = "session.deleted"

    BENCH_TX_ENABLED = "bench_tx.enabled"
    BENCH_TX_DISABLED = "bench_tx.disabled"

    LISTEN_STARTED = "listen.started"
    LISTEN_ENDED = "listen.ended"

    PEER_ACTIVATED = "peer.activated"
    PEER_DEACTIVATED = "peer.deactivated"

    INJECTION_SUBMITTED = "injection.submitted"
    INJECTION_COMPLETED = "injection.completed"
    INJECTION_CANCELLED = "injection.cancelled"

    CONFLICT_DETECTED = "conflict.detected"

    STATUS_UPDATE = "status.update"


@dataclass
class StateEvent:
    """Real-time event to stream to UI via WebSocket."""

    type: EventType
    session_id: str
    timestamp: datetime
    data: dict

    def to_dict(self) -> dict:
        """Convert to JSON-serializable dict."""
        return {
            "type": self.type.value,
            "session_id": self.session_id,
            "timestamp": self.timestamp.isoformat(),
            "data": self.data,
        }
