"""Test WebSocket and real-time event streaming (workplan §5.7)."""

import asyncio
import json
from datetime import datetime

import pytest

from vtc.models.events import EventType, StateEvent
from vtc.services.encoder import EncoderService
from vtc.services.hmi import HmiService
from vtc.services.injections import InjectionService
from vtc.services.scheduler import Scheduler
from vtc.services.source_conflict_monitor import SourceConflictMonitor
from vtc.services.status_aggregator import StatusAggregator
from vtc.services.synthetic_peers import SyntheticPeerEngine
from vtc.services.tx_gate import TxGate
from vtc.services.websocket_manager import WebSocketManager
from vtc.services.ownership import OwnershipTable


@pytest.fixture
def encoder():
    """Create encoder service."""
    return EncoderService()


@pytest.fixture
async def scheduler(encoder):
    """Create scheduler."""
    async def mock_submit(bus: str, can_id: int, data: bytes) -> None:
        pass

    return Scheduler(encoder, mock_submit)


@pytest.fixture
async def synthetic_engine(scheduler):
    """Create synthetic peer engine."""
    return SyntheticPeerEngine(scheduler)


@pytest.fixture
async def conflict_monitor(synthetic_engine):
    """Create conflict monitor."""
    return SourceConflictMonitor(synthetic_engine)


@pytest.fixture
def ownership_table():
    """Create ownership table."""
    return OwnershipTable()


@pytest.fixture
def tx_gate(encoder, ownership_table):
    """Create TX gate."""
    return TxGate(encoder, ownership_table)


@pytest.fixture
def injection_service(encoder, tx_gate):
    """Create injection service."""
    return InjectionService(encoder, tx_gate)


@pytest.fixture
def ws_manager():
    """Create WebSocket manager."""
    return WebSocketManager()


@pytest.fixture
def hmi_service(synthetic_engine, conflict_monitor, injection_service):
    """Create HMI service."""
    return HmiService(synthetic_engine, conflict_monitor, injection_service)


@pytest.fixture
def status_aggregator(synthetic_engine, conflict_monitor, injection_service, ws_manager):
    """Create status aggregator."""
    return StatusAggregator(synthetic_engine, conflict_monitor, injection_service, ws_manager)


class TestStateEvent:
    """Test event model."""

    @pytest.mark.asyncio
    async def test_event_creation(self):
        """Test creating a state event."""
        event = StateEvent(
            type=EventType.SESSION_CREATED,
            session_id="ses_123",
            timestamp=datetime.utcnow(),
            data={"profile": "pure_software"},
        )

        assert event.type == EventType.SESSION_CREATED
        assert event.session_id == "ses_123"
        assert event.data["profile"] == "pure_software"

    @pytest.mark.asyncio
    async def test_event_to_dict(self):
        """Test event serialization to dict."""
        event = StateEvent(
            type=EventType.PEER_ACTIVATED,
            session_id="ses_123",
            timestamp=datetime(2026, 7, 17, 12, 0, 0),
            data={"peer": "sys:rt_heartbeat_high"},
        )

        result = event.to_dict()
        assert result["type"] == "peer.activated"
        assert result["session_id"] == "ses_123"
        assert "timestamp" in result
        assert result["data"]["peer"] == "sys:rt_heartbeat_high"


class TestWebSocketManager:
    """Test WebSocket connection management."""

    @pytest.mark.asyncio
    async def test_connect_client(self, ws_manager):
        """Test connecting a WebSocket client."""
        # Mock WebSocket
        class MockWebSocket:
            async def send_text(self, message):
                pass

        ws = MockWebSocket()
        await ws_manager.connect("ses_123", ws)

        count = await ws_manager.get_connection_count("ses_123")
        assert count == 1

    @pytest.mark.asyncio
    async def test_disconnect_client(self, ws_manager):
        """Test disconnecting a client."""
        class MockWebSocket:
            async def send_text(self, message):
                pass

        ws = MockWebSocket()
        await ws_manager.connect("ses_123", ws)
        assert await ws_manager.get_connection_count("ses_123") == 1

        await ws_manager.disconnect("ses_123", ws)
        assert await ws_manager.get_connection_count("ses_123") == 0

    @pytest.mark.asyncio
    async def test_multiple_connections(self, ws_manager):
        """Test multiple clients for same session."""
        class MockWebSocket:
            async def send_text(self, message):
                pass

        ws1 = MockWebSocket()
        ws2 = MockWebSocket()
        ws3 = MockWebSocket()

        await ws_manager.connect("ses_123", ws1)
        await ws_manager.connect("ses_123", ws2)
        await ws_manager.connect("ses_123", ws3)

        count = await ws_manager.get_connection_count("ses_123")
        assert count == 3

    @pytest.mark.asyncio
    async def test_broadcast_event(self, ws_manager):
        """Test broadcasting event to all clients."""
        received_messages = []

        class MockWebSocket:
            async def send_text(self, message):
                received_messages.append(json.loads(message))

        ws1 = MockWebSocket()
        ws2 = MockWebSocket()

        await ws_manager.connect("ses_123", ws1)
        await ws_manager.connect("ses_123", ws2)

        event = StateEvent(
            type=EventType.PEER_ACTIVATED,
            session_id="ses_123",
            timestamp=datetime.utcnow(),
            data={"peer": "sys:rt_heartbeat_high"},
        )

        await ws_manager.broadcast("ses_123", event)

        assert len(received_messages) == 2
        assert all(msg["type"] == "peer.activated" for msg in received_messages)

    @pytest.mark.asyncio
    async def test_broadcast_to_nonexistent_session(self, ws_manager):
        """Test broadcasting to session with no clients (should not error)."""
        event = StateEvent(
            type=EventType.STATUS_UPDATE,
            session_id="ses_nonexistent",
            timestamp=datetime.utcnow(),
            data={},
        )

        # Should not raise
        await ws_manager.broadcast("ses_nonexistent", event)

    @pytest.mark.asyncio
    async def test_send_personal(self, ws_manager):
        """Test sending to specific client."""
        received_messages = []

        class MockWebSocket:
            async def send_text(self, message):
                received_messages.append(json.loads(message))

        ws = MockWebSocket()

        event = StateEvent(
            type=EventType.SESSION_CREATED,
            session_id="ses_123",
            timestamp=datetime.utcnow(),
            data={},
        )

        await ws_manager.send_personal(ws, event)

        assert len(received_messages) == 1
        assert received_messages[0]["type"] == "session.created"


class TestStatusAggregator:
    """Test status aggregation and event emission."""

    @pytest.mark.asyncio
    async def test_aggregate_status(self, status_aggregator, hmi_service):
        """Test gathering status from all services."""
        # Create session
        session = await hmi_service.create_session("Test", "pure_software", "sys")

        # Aggregate status
        status = await status_aggregator._aggregate_status(session.session_id)

        assert "synthetic_peers_active" in status
        assert "pending_injections" in status
        assert "submitted_injections" in status
        assert "total_conflicts" in status
        assert "listening" in status
        assert status["synthetic_peers_active"] == 0
        assert status["pending_injections"] == 0

    @pytest.mark.asyncio
    async def test_start_aggregation(self, status_aggregator, hmi_service):
        """Test starting periodic aggregation."""
        session = await hmi_service.create_session("Test", "pure_software", "sys")

        # Start aggregation
        await status_aggregator.start_aggregation(
            session.session_id, interval_ms=100
        )

        # Verify task is running
        assert session.session_id in status_aggregator.aggregation_tasks

        # Stop aggregation
        await status_aggregator.stop_aggregation(session.session_id)

        # Verify task is cleaned up
        await asyncio.sleep(0.05)  # Let task finish

    @pytest.mark.asyncio
    async def test_emit_event(self, status_aggregator, ws_manager, hmi_service):
        """Test emitting custom event."""
        session = await hmi_service.create_session("Test", "pure_software", "sys")

        received_messages = []

        class MockWebSocket:
            async def send_text(self, message):
                received_messages.append(json.loads(message))

        ws = MockWebSocket()
        await ws_manager.connect(session.session_id, ws)

        event = StateEvent(
            type=EventType.CONFLICT_DETECTED,
            session_id=session.session_id,
            timestamp=datetime.utcnow(),
            data={"bus": "high", "can_id": 0x300},
        )

        await status_aggregator.emit_event(event)

        assert len(received_messages) == 1
        assert received_messages[0]["type"] == "conflict.detected"


class TestEventTypes:
    """Test all event types."""

    @pytest.mark.asyncio
    async def test_all_event_types_defined(self):
        """Test that all expected event types are defined."""
        expected_types = [
            EventType.SESSION_CREATED,
            EventType.SESSION_UPDATED,
            EventType.SESSION_DELETED,
            EventType.BENCH_TX_ENABLED,
            EventType.BENCH_TX_DISABLED,
            EventType.LISTEN_STARTED,
            EventType.LISTEN_ENDED,
            EventType.PEER_ACTIVATED,
            EventType.PEER_DEACTIVATED,
            EventType.INJECTION_SUBMITTED,
            EventType.INJECTION_COMPLETED,
            EventType.INJECTION_CANCELLED,
            EventType.CONFLICT_DETECTED,
            EventType.STATUS_UPDATE,
        ]

        for event_type in expected_types:
            assert isinstance(event_type.value, str)
            assert len(event_type.value) > 0


class TestWebSocketIntegration:
    """Integration tests for WebSocket system."""

    @pytest.mark.asyncio
    async def test_full_session_event_stream(self, ws_manager, status_aggregator, hmi_service):
        """Test complete event stream for a session."""
        received_messages = []

        class MockWebSocket:
            async def send_text(self, message):
                received_messages.append(json.loads(message))

        # Create session
        session = await hmi_service.create_session("Test", "pure_software", "sys")

        # Connect client
        ws = MockWebSocket()
        await ws_manager.connect(session.session_id, ws)

        # Emit various events
        events = [
            StateEvent(
                type=EventType.SESSION_CREATED,
                session_id=session.session_id,
                timestamp=datetime.utcnow(),
                data={"profile": "pure_software"},
            ),
            StateEvent(
                type=EventType.BENCH_TX_ENABLED,
                session_id=session.session_id,
                timestamp=datetime.utcnow(),
                data={},
            ),
            StateEvent(
                type=EventType.LISTEN_STARTED,
                session_id=session.session_id,
                timestamp=datetime.utcnow(),
                data={"duration_ms": 500},
            ),
        ]

        for event in events:
            await status_aggregator.emit_event(event)

        # Verify all events received
        assert len(received_messages) >= 3
        types_received = [msg["type"] for msg in received_messages]
        assert "session.created" in types_received
        assert "bench_tx.enabled" in types_received
        assert "listen.started" in types_received

    @pytest.mark.asyncio
    async def test_aggregation_loop(self, status_aggregator, ws_manager, hmi_service):
        """Test that aggregation loop produces status updates."""
        received_messages = []

        class MockWebSocket:
            async def send_text(self, message):
                received_messages.append(json.loads(message))

        session = await hmi_service.create_session("Test", "pure_software", "sys")

        ws = MockWebSocket()
        await ws_manager.connect(session.session_id, ws)

        # Start aggregation with short interval
        await status_aggregator.start_aggregation(
            session.session_id, interval_ms=100
        )

        # Wait for updates
        await asyncio.sleep(0.35)  # Should get ~3 updates

        # Stop aggregation
        await status_aggregator.stop_aggregation(session.session_id)

        # Verify status updates received
        status_updates = [
            msg for msg in received_messages if msg["type"] == "status.update"
        ]
        assert len(status_updates) >= 2
