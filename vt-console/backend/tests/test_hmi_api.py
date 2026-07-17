"""Test HMI API endpoints (workplan §5.7)."""

import pytest
from fastapi import FastAPI
from fastapi.testclient import TestClient

from vtc.api.hmi import router
from vtc.services.encoder import EncoderService
from vtc.services.hmi import HmiService
from vtc.services.injections import InjectionService
from vtc.services.scheduler import Scheduler
from vtc.services.source_conflict_monitor import SourceConflictMonitor
from vtc.services.synthetic_peers import SyntheticPeerEngine
from vtc.services.tx_gate import TxGate
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
def hmi_service(synthetic_engine, conflict_monitor, injection_service):
    """Create HMI service."""
    return HmiService(synthetic_engine, conflict_monitor, injection_service)


@pytest.fixture
def app(hmi_service):
    """Create FastAPI test app."""
    app = FastAPI()

    from vtc.api.hmi import get_hmi_service

    app.dependency_overrides[get_hmi_service] = lambda: hmi_service

    app.include_router(router)
    return app


@pytest.fixture
def client(app):
    """Create test client."""
    return TestClient(app)


class TestCreateSession:
    """Test creating sessions."""

    @pytest.mark.asyncio
    async def test_create_session(self, client):
        """Test creating a session."""
        response = client.post(
            "/api/v1/sessions",
            json={
                "name": "Test Session",
                "profile": "pure_software",
                "dut": "sys",
            },
        )

        assert response.status_code == 200
        data = response.json()
        assert "session_id" in data
        assert data["session_id"].startswith("ses_")
        assert data["name"] == "Test Session"
        assert data["profile"] == "pure_software"
        assert data["dut"] == "sys"
        assert "created_at" in data

    @pytest.mark.asyncio
    async def test_create_session_invalid_profile(self, client):
        """Test error on invalid profile."""
        response = client.post(
            "/api/v1/sessions",
            json={
                "name": "Test",
                "profile": "invalid_profile",
                "dut": "sys",
            },
        )

        assert response.status_code == 400

    @pytest.mark.asyncio
    async def test_create_session_invalid_dut(self, client):
        """Test error on invalid DUT."""
        response = client.post(
            "/api/v1/sessions",
            json={
                "name": "Test",
                "profile": "pure_software",
                "dut": "invalid",
            },
        )

        assert response.status_code == 400

    @pytest.mark.asyncio
    async def test_create_session_missing_dut(self, client):
        """Test error on missing required DUT."""
        response = client.post(
            "/api/v1/sessions",
            json={
                "name": "Test",
                "profile": "pure_software",
            },
        )

        assert response.status_code == 422


class TestListSessions:
    """Test listing sessions."""

    @pytest.mark.asyncio
    async def test_list_sessions_empty(self, client):
        """Test listing when no sessions exist."""
        response = client.get("/api/v1/sessions")

        assert response.status_code == 200
        data = response.json()
        assert data["total_count"] == 0
        assert data["sessions"] == []

    @pytest.mark.asyncio
    async def test_list_sessions_multiple(self, client):
        """Test listing multiple sessions."""
        # Create 3 sessions
        for i in range(3):
            client.post(
                "/api/v1/sessions",
                json={
                    "name": f"Session {i}",
                    "profile": "pure_software",
                    "dut": "sys",
                },
            )

        response = client.get("/api/v1/sessions")

        assert response.status_code == 200
        data = response.json()
        assert data["total_count"] >= 3
        assert len(data["sessions"]) >= 3


class TestGetSession:
    """Test getting session details."""

    @pytest.mark.asyncio
    async def test_get_session(self, client):
        """Test getting session details."""
        # Create session
        create_response = client.post(
            "/api/v1/sessions",
            json={
                "name": "Test Session",
                "profile": "pure_software",
                "dut": "sys",
            },
        )
        session_id = create_response.json()["session_id"]

        # Get it
        response = client.get(f"/api/v1/sessions/{session_id}")

        assert response.status_code == 200
        data = response.json()
        assert data["session"]["session_id"] == session_id
        assert data["session"]["name"] == "Test Session"

    @pytest.mark.asyncio
    async def test_get_nonexistent_session(self, client):
        """Test error on nonexistent session."""
        response = client.get("/api/v1/sessions/ses_nonexistent")

        assert response.status_code == 404


class TestUpdateSession:
    """Test updating session settings."""

    @pytest.mark.asyncio
    async def test_update_session_enable_bench_tx(self, client):
        """Test enabling Bench TX."""
        # Create session
        create_response = client.post(
            "/api/v1/sessions",
            json={
                "name": "Test",
                "profile": "pure_software",
                "dut": "sys",
            },
        )
        session_id = create_response.json()["session_id"]

        # Update
        response = client.put(
            f"/api/v1/sessions/{session_id}",
            json={"bench_tx_enabled": True},
        )

        assert response.status_code == 200
        data = response.json()
        assert data["bench_tx_enabled"] is True

    @pytest.mark.asyncio
    async def test_update_session_profile(self, client):
        """Test changing profile."""
        # Create session
        create_response = client.post(
            "/api/v1/sessions",
            json={
                "name": "Test",
                "profile": "pure_software",
                "dut": "sys",
            },
        )
        session_id = create_response.json()["session_id"]

        # Update profile
        response = client.put(
            f"/api/v1/sessions/{session_id}",
            json={"profile": "bench_test"},
        )

        assert response.status_code == 200
        data = response.json()
        assert data["profile"] == "bench_test"

    @pytest.mark.asyncio
    async def test_update_nonexistent_session(self, client):
        """Test error on nonexistent session."""
        response = client.put(
            "/api/v1/sessions/ses_nonexistent",
            json={"bench_tx_enabled": True},
        )

        assert response.status_code == 404


class TestDeleteSession:
    """Test deleting sessions."""

    @pytest.mark.asyncio
    async def test_delete_session(self, client):
        """Test deleting a session."""
        # Create session
        create_response = client.post(
            "/api/v1/sessions",
            json={
                "name": "Test",
                "profile": "pure_software",
                "dut": "sys",
            },
        )
        session_id = create_response.json()["session_id"]

        # Delete
        response = client.delete(f"/api/v1/sessions/{session_id}")

        assert response.status_code == 200
        data = response.json()
        assert data["session_id"] == session_id
        assert data["status"] == "deleted"

        # Verify deleted
        get_response = client.get(f"/api/v1/sessions/{session_id}")
        assert get_response.status_code == 404

    @pytest.mark.asyncio
    async def test_delete_nonexistent_session(self, client):
        """Test error on nonexistent session."""
        response = client.delete("/api/v1/sessions/ses_nonexistent")

        assert response.status_code == 404


class TestBenchTest:
    """Test bench testing workflows."""

    @pytest.mark.asyncio
    async def test_start_bench_test(self, client):
        """Test starting bench test."""
        # Create session
        create_response = client.post(
            "/api/v1/sessions",
            json={
                "name": "Test",
                "profile": "pure_software",
                "dut": "sys",
            },
        )
        session_id = create_response.json()["session_id"]

        # Start bench test
        response = client.post(
            f"/api/v1/sessions/{session_id}/bench-test/start",
            json={"dut": "sys", "listen_duration_ms": 500},
        )

        assert response.status_code == 200
        data = response.json()
        assert data["session_id"] == session_id
        assert data["status"] == "listening"
        assert data["listening"] is True
        assert 0 < data["listening_remaining_ms"] <= 500

    @pytest.mark.asyncio
    async def test_start_bench_test_invalid_dut(self, client):
        """Test error on invalid DUT."""
        # Create session
        create_response = client.post(
            "/api/v1/sessions",
            json={
                "name": "Test",
                "profile": "pure_software",
                "dut": "sys",
            },
        )
        session_id = create_response.json()["session_id"]

        # Start with invalid DUT
        response = client.post(
            f"/api/v1/sessions/{session_id}/bench-test/start",
            json={"dut": "invalid", "listen_duration_ms": 500},
        )

        assert response.status_code == 400

    @pytest.mark.asyncio
    async def test_stop_bench_test(self, client):
        """Test stopping bench test."""
        # Create session
        create_response = client.post(
            "/api/v1/sessions",
            json={
                "name": "Test",
                "profile": "pure_software",
                "dut": "sys",
            },
        )
        session_id = create_response.json()["session_id"]

        # Start bench test
        client.post(
            f"/api/v1/sessions/{session_id}/bench-test/start",
            json={"dut": "sys"},
        )

        # Stop
        response = client.post(
            f"/api/v1/sessions/{session_id}/bench-test/stop"
        )

        assert response.status_code == 200
        data = response.json()
        assert data["session_id"] == session_id
        assert data["status"] == "stopped"
        assert "synthetic_peers_stopped" in data
        assert "pending_injections_cancelled" in data


class TestSystemStatus:
    """Test status endpoints."""

    @pytest.mark.asyncio
    async def test_get_system_status(self, client):
        """Test getting system status."""
        # Create session
        create_response = client.post(
            "/api/v1/sessions",
            json={
                "name": "Test",
                "profile": "pure_software",
                "dut": "sys",
            },
        )
        session_id = create_response.json()["session_id"]

        # Get status
        response = client.get(f"/api/v1/sessions/{session_id}/status")

        assert response.status_code == 200
        data = response.json()
        assert data["session_id"] == session_id
        assert data["profile"] == "pure_software"
        assert data["dut"] == "sys"
        assert "bench_tx_enabled" in data
        assert "synthetic_peers_active" in data
        assert "pending_injections" in data
        assert "total_conflicts" in data

    @pytest.mark.asyncio
    async def test_get_control_workspace_status(self, client):
        """Test getting control workspace status."""
        # Create session
        create_response = client.post(
            "/api/v1/sessions",
            json={
                "name": "Test",
                "profile": "pure_software",
                "dut": "sys",
            },
        )
        session_id = create_response.json()["session_id"]

        # Get status
        response = client.get(
            f"/api/v1/sessions/{session_id}/control-workspace-status"
        )

        assert response.status_code == 200
        data = response.json()
        assert data["session_id"] == session_id
        assert "synthetic_peers" in data
        assert "injections" in data
        assert "conflicts" in data


class TestFullWorkflow:
    """Test complete workflows."""

    @pytest.mark.asyncio
    async def test_full_session_workflow(self, client):
        """Test complete session lifecycle."""
        # 1. Create session
        create_response = client.post(
            "/api/v1/sessions",
            json={
                "name": "Full Workflow Test",
                "profile": "pure_software",
                "dut": "sys",
            },
        )
        assert create_response.status_code == 200
        session_id = create_response.json()["session_id"]

        # 2. List sessions
        list_response = client.get("/api/v1/sessions")
        assert list_response.status_code == 200
        assert any(s["session_id"] == session_id for s in list_response.json()["sessions"])

        # 3. Get session details
        get_response = client.get(f"/api/v1/sessions/{session_id}")
        assert get_response.status_code == 200
        assert get_response.json()["session"]["session_id"] == session_id

        # 4. Update session
        update_response = client.put(
            f"/api/v1/sessions/{session_id}",
            json={"bench_tx_enabled": True},
        )
        assert update_response.status_code == 200

        # 5. Get status
        status_response = client.get(f"/api/v1/sessions/{session_id}/status")
        assert status_response.status_code == 200

        # 6. Start bench test
        bench_start_response = client.post(
            f"/api/v1/sessions/{session_id}/bench-test/start",
            json={"dut": "sys"},
        )
        assert bench_start_response.status_code == 200

        # 7. Stop bench test
        bench_stop_response = client.post(
            f"/api/v1/sessions/{session_id}/bench-test/stop"
        )
        assert bench_stop_response.status_code == 200

        # 8. Delete session
        delete_response = client.delete(f"/api/v1/sessions/{session_id}")
        assert delete_response.status_code == 200

        # 9. Verify deleted
        get_deleted = client.get(f"/api/v1/sessions/{session_id}")
        assert get_deleted.status_code == 404
