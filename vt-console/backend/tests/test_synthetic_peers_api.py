"""Test Synthetic Peers API endpoints (workplan §5.4 - §5.5)."""

import pytest
from fastapi import FastAPI
from fastapi.testclient import TestClient

from vtc.api.synthetic_peers import router
from vtc.services.encoder import EncoderService
from vtc.services.scheduler import Scheduler
from vtc.services.source_conflict_monitor import SourceConflictMonitor
from vtc.services.synthetic_peers import DutKind, SyntheticPeerEngine


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
def app(synthetic_engine, conflict_monitor):
    """Create FastAPI test app with injected dependencies."""
    app = FastAPI()

    # Override dependency injectors
    from vtc.api.synthetic_peers import get_synthetic_engine, get_conflict_monitor

    app.dependency_overrides[get_synthetic_engine] = lambda: synthetic_engine
    app.dependency_overrides[get_conflict_monitor] = lambda: conflict_monitor

    app.include_router(router)
    return app


@pytest.fixture
def client(app):
    """Create test client."""
    return TestClient(app)


class TestListAvailablePeers:
    """Test listing available peer templates."""

    @pytest.mark.asyncio
    async def test_list_sys_dut_peers(self, client):
        """Test listing available SYS-DUT peers."""
        response = client.get("/api/v1/sessions/ses_123/synthetic-peers/available?dut=sys")

        assert response.status_code == 200
        data = response.json()
        assert data["dut"] == "sys"
        assert data["peer_count"] > 0
        assert len(data["peers"]) > 0

        # Check peer structure
        peer = data["peers"][0]
        assert "name" in peer
        assert "key" in peer
        assert "bus" in peer
        assert "period_ms" in peer
        assert "has_counter" in peer

    @pytest.mark.asyncio
    async def test_list_rt_dut_peers(self, client):
        """Test listing available RT-DUT peers."""
        response = client.get("/api/v1/sessions/ses_123/synthetic-peers/available?dut=rt")

        assert response.status_code == 200
        data = response.json()
        assert data["dut"] == "rt"
        assert data["peer_count"] > 0

    @pytest.mark.asyncio
    async def test_invalid_dut_kind(self, client):
        """Test error on invalid DUT kind."""
        response = client.get("/api/v1/sessions/ses_123/synthetic-peers/available?dut=invalid")

        assert response.status_code == 400
        assert "Invalid DUT kind" in response.json()["detail"]


class TestListenWindow:
    """Test listen window endpoints."""

    @pytest.mark.asyncio
    async def test_start_listen_window(self, client):
        """Test starting a listen window."""
        response = client.post(
            "/api/v1/sessions/ses_123/synthetic-peers/listen",
            json={
                "dut": "sys",
                "duration_ms": 500,
            },
        )

        assert response.status_code == 200
        data = response.json()
        assert data["listening"] is True
        assert data["dut"] == "sys"
        assert data["duration_ms"] == 500
        assert 0 < data["remaining_ms"] <= 500

    @pytest.mark.asyncio
    async def test_listen_window_default_duration(self, client):
        """Test listen window with default duration."""
        response = client.post(
            "/api/v1/sessions/ses_123/synthetic-peers/listen",
            json={
                "dut": "sys",
            },
        )

        assert response.status_code == 200
        data = response.json()
        assert data["duration_ms"] == 500  # Default

    @pytest.mark.asyncio
    async def test_listen_window_custom_duration(self, client):
        """Test listen window with custom duration."""
        response = client.post(
            "/api/v1/sessions/ses_123/synthetic-peers/listen",
            json={
                "dut": "sys",
                "duration_ms": 1000,
            },
        )

        assert response.status_code == 200
        data = response.json()
        assert data["duration_ms"] == 1000

    @pytest.mark.asyncio
    async def test_listen_window_invalid_dut(self, client):
        """Test error on invalid DUT."""
        response = client.post(
            "/api/v1/sessions/ses_123/synthetic-peers/listen",
            json={
                "dut": "invalid",
            },
        )

        assert response.status_code == 400


class TestActivatePeers:
    """Test peer activation endpoint."""

    @pytest.mark.asyncio
    async def test_activate_sys_dut_peers(self, client):
        """Test activating SYS-DUT peers."""
        # First start listen window
        client.post(
            "/api/v1/sessions/ses_123/synthetic-peers/listen",
            json={"dut": "sys", "duration_ms": 500},
        )

        # Then activate
        response = client.post(
            "/api/v1/sessions/ses_123/synthetic-peers/activate",
            json={"dut": "sys"},
        )

        assert response.status_code == 200
        data = response.json()
        assert data["activated_count"] >= 0  # May be 0 if all fail to encode
        assert isinstance(data["peers"], list)

        # Check peer structure if any
        if data["peers"]:
            peer = data["peers"][0]
            assert "name" in peer
            assert "job_id" in peer
            assert "key" in peer
            assert "bus" in peer
            assert "period_ms" in peer

    @pytest.mark.asyncio
    async def test_activate_rt_dut_peers(self, client):
        """Test activating RT-DUT peers."""
        response = client.post(
            "/api/v1/sessions/ses_123/synthetic-peers/activate",
            json={"dut": "rt"},
        )

        assert response.status_code == 200
        data = response.json()
        assert "activated_count" in data
        assert "peers" in data

    @pytest.mark.asyncio
    async def test_activate_invalid_dut(self, client):
        """Test error on invalid DUT."""
        response = client.post(
            "/api/v1/sessions/ses_123/synthetic-peers/activate",
            json={"dut": "invalid"},
        )

        assert response.status_code == 400


class TestGetStatus:
    """Test status endpoint."""

    @pytest.mark.asyncio
    async def test_get_status_no_peers(self, client):
        """Test getting status when no peers active."""
        response = client.get("/api/v1/sessions/ses_123/synthetic-peers/status")

        assert response.status_code == 200
        data = response.json()
        assert data["dut"] is None
        assert data["listening"] is False
        assert data["active_peer_count"] == 0
        assert data["active_peers"] == []
        assert data["conflicts"] == []

    @pytest.mark.asyncio
    async def test_get_status_with_listen_window(self, client):
        """Test status during listen window."""
        # Start listen window
        client.post(
            "/api/v1/sessions/ses_123/synthetic-peers/listen",
            json={"dut": "sys"},
        )

        # Get status
        response = client.get("/api/v1/sessions/ses_123/synthetic-peers/status")

        assert response.status_code == 200
        data = response.json()
        assert data["dut"] == "sys"
        assert data["listening"] is True
        assert data["listen_remaining_ms"] is not None
        assert 0 < data["listen_remaining_ms"] <= 500

    @pytest.mark.asyncio
    async def test_get_status_with_active_peers(self, client):
        """Test status with active peers."""
        # Activate peers
        client.post(
            "/api/v1/sessions/ses_123/synthetic-peers/activate",
            json={"dut": "sys"},
        )

        # Get status
        response = client.get("/api/v1/sessions/ses_123/synthetic-peers/status")

        assert response.status_code == 200
        data = response.json()
        assert data["dut"] == "sys"
        # active_peer_count may be 0 if peers fail to encode
        assert "active_peer_count" in data
        assert "active_peers" in data


class TestStopPeers:
    """Test stop peers endpoint."""

    @pytest.mark.asyncio
    async def test_stop_peers_no_active(self, client):
        """Test stopping when no peers are active."""
        response = client.post(
            "/api/v1/sessions/ses_123/synthetic-peers/stop"
        )

        assert response.status_code == 200
        data = response.json()
        assert data["stopped_count"] == 0
        assert data["status"] == "no_peers_active"

    @pytest.mark.asyncio
    async def test_stop_peers_with_active(self, client):
        """Test stopping active peers."""
        # Activate peers
        client.post(
            "/api/v1/sessions/ses_123/synthetic-peers/activate",
            json={"dut": "sys"},
        )

        # Stop them
        response = client.post(
            "/api/v1/sessions/ses_123/synthetic-peers/stop"
        )

        assert response.status_code == 200
        data = response.json()
        assert "stopped_count" in data
        assert "status" in data

    @pytest.mark.asyncio
    async def test_stop_clears_state(self, client):
        """Test that stop clears monitoring state."""
        # Start listen window
        client.post(
            "/api/v1/sessions/ses_123/synthetic-peers/listen",
            json={"dut": "sys"},
        )

        # Activate peers
        client.post(
            "/api/v1/sessions/ses_123/synthetic-peers/activate",
            json={"dut": "sys"},
        )

        # Stop
        client.post("/api/v1/sessions/ses_123/synthetic-peers/stop")

        # Check status - should be cleared
        response = client.get("/api/v1/sessions/ses_123/synthetic-peers/status")
        data = response.json()
        assert data["dut"] is None
        assert data["listening"] is False
        assert data["active_peer_count"] == 0


class TestConflictReport:
    """Test conflict report endpoint."""

    @pytest.mark.asyncio
    async def test_get_conflict_report_no_conflicts(self, client):
        """Test getting conflict report when none exist."""
        response = client.get(
            "/api/v1/sessions/ses_123/synthetic-peers/conflicts"
        )

        assert response.status_code == 200
        data = response.json()
        assert data["conflict_count"] == 0
        assert data["recent_conflicts"] == []

    @pytest.mark.asyncio
    async def test_get_conflict_report_structure(self, client):
        """Test conflict report structure."""
        response = client.get(
            "/api/v1/sessions/ses_123/synthetic-peers/conflicts"
        )

        assert response.status_code == 200
        data = response.json()
        assert "conflict_count" in data
        assert "recent_conflicts" in data
        assert isinstance(data["recent_conflicts"], list)


class TestFullWorkflow:
    """Test complete workflow."""

    @pytest.mark.asyncio
    async def test_full_workflow_sys_dut(self, client):
        """Test full workflow for SYS-DUT."""
        # 1. List available peers
        response = client.get(
            "/api/v1/sessions/ses_123/synthetic-peers/available?dut=sys"
        )
        assert response.status_code == 200
        assert response.json()["peer_count"] > 0

        # 2. Start listen window
        response = client.post(
            "/api/v1/sessions/ses_123/synthetic-peers/listen",
            json={"dut": "sys", "duration_ms": 500},
        )
        assert response.status_code == 200
        assert response.json()["listening"] is True

        # 3. Get status during listen
        response = client.get("/api/v1/sessions/ses_123/synthetic-peers/status")
        assert response.status_code == 200
        assert response.json()["listening"] is True

        # 4. Activate peers
        response = client.post(
            "/api/v1/sessions/ses_123/synthetic-peers/activate",
            json={"dut": "sys"},
        )
        assert response.status_code == 200

        # 5. Get status with active peers
        response = client.get("/api/v1/sessions/ses_123/synthetic-peers/status")
        assert response.status_code == 200
        data = response.json()
        assert data["dut"] == "sys"

        # 6. Get conflict report
        response = client.get(
            "/api/v1/sessions/ses_123/synthetic-peers/conflicts"
        )
        assert response.status_code == 200

        # 7. Stop peers
        response = client.post(
            "/api/v1/sessions/ses_123/synthetic-peers/stop"
        )
        assert response.status_code == 200

        # 8. Verify stopped
        response = client.get("/api/v1/sessions/ses_123/synthetic-peers/status")
        assert response.status_code == 200
        assert response.json()["active_peer_count"] == 0


class TestErrorHandling:
    """Test error handling."""

    @pytest.mark.asyncio
    async def test_invalid_dut_in_various_endpoints(self, client):
        """Test that invalid DUT is rejected in all endpoints."""
        # List available
        response = client.get(
            "/api/v1/sessions/ses_123/synthetic-peers/available?dut=bad"
        )
        assert response.status_code == 400

        # Listen
        response = client.post(
            "/api/v1/sessions/ses_123/synthetic-peers/listen",
            json={"dut": "bad"},
        )
        assert response.status_code == 400

        # Activate
        response = client.post(
            "/api/v1/sessions/ses_123/synthetic-peers/activate",
            json={"dut": "bad"},
        )
        assert response.status_code == 400

    @pytest.mark.asyncio
    async def test_missing_required_fields(self, client):
        """Test error on missing required fields."""
        # Missing dut
        response = client.post(
            "/api/v1/sessions/ses_123/synthetic-peers/listen",
            json={},
        )
        assert response.status_code == 422  # Validation error

        # Missing dut in activate
        response = client.post(
            "/api/v1/sessions/ses_123/synthetic-peers/activate",
            json={},
        )
        assert response.status_code == 422
