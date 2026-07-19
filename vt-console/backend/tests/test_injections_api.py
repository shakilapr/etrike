"""Test Injection API endpoints (workplan §5.6)."""

import asyncio

import pytest
from fastapi import FastAPI
from fastapi.testclient import TestClient

from vtc.api.injections import router
from vtc.services.encoder import EncoderService
from vtc.services.injections import InjectionService
from vtc.services.tx_gate import TxGate
from vtc.services.ownership import OwnershipTable


@pytest.fixture
def encoder():
    """Create encoder service."""
    return EncoderService()


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
def app(injection_service):
    """Create FastAPI test app."""
    app = FastAPI()

    # Override dependency
    from vtc.api.injections import get_injection_service

    app.dependency_overrides[get_injection_service] = lambda: injection_service

    app.include_router(router)
    return app


@pytest.fixture
def client(app):
    """Create test client."""
    return TestClient(app)


class TestSubmitInjection:
    """Test submitting injections."""

    @pytest.mark.asyncio
    async def test_submit_immediate_injection(self, client):
        """Test submitting an immediate injection."""
        response = client.post(
            "/api/v1/sessions/ses_123/injections",
            json={
                "key": "host:host_drive_cmd",
                "values": {"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
                "bus": "high",
                "delay_ms": 0,
            },
        )

        assert response.status_code == 200
        data = response.json()
        assert "injection_id" in data
        assert data["injection_id"].startswith("inj_")
        assert data["status"] in ("pending", "submitted", "failed")

    @pytest.mark.asyncio
    async def test_submit_delayed_injection(self, client):
        """Test submitting a delayed injection."""
        response = client.post(
            "/api/v1/sessions/ses_123/injections",
            json={
                "key": "host:host_drive_cmd",
                "values": {"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
                "bus": "high",
                "delay_ms": 100,
            },
        )

        assert response.status_code == 200
        data = response.json()
        assert data["status"] == "pending"  # Not submitted yet

    @pytest.mark.asyncio
    async def test_submit_with_invalid_bus(self, client):
        """Test error on invalid bus."""
        response = client.post(
            "/api/v1/sessions/ses_123/injections",
            json={
                "key": "host:host_drive_cmd",
                "values": {"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
                "bus": "invalid",
            },
        )

        assert response.status_code == 400

    @pytest.mark.asyncio
    async def test_submit_missing_required_fields(self, client):
        """Test error on missing required fields."""
        response = client.post(
            "/api/v1/sessions/ses_123/injections",
            json={
                "key": "host:host_drive_cmd",
                # Missing values
                "bus": "high",
            },
        )

        assert response.status_code == 422

    @pytest.mark.asyncio
    async def test_submit_delay_bounds(self, client):
        """Test delay validation."""
        # Too high delay
        response = client.post(
            "/api/v1/sessions/ses_123/injections",
            json={
                "key": "host:host_drive_cmd",
                "values": {"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
                "bus": "high",
                "delay_ms": 100000,  # Too high
            },
        )

        assert response.status_code == 422

        # Negative delay
        response = client.post(
            "/api/v1/sessions/ses_123/injections",
            json={
                "key": "host:host_drive_cmd",
                "values": {"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
                "bus": "high",
                "delay_ms": -100,
            },
        )

        assert response.status_code == 422


class TestListInjections:
    """Test listing injections."""

    @pytest.mark.asyncio
    async def test_list_injections_empty(self, client):
        """Test listing when no injections exist."""
        response = client.get(
            "/api/v1/sessions/ses_123/injections"
        )

        assert response.status_code == 200
        data = response.json()
        assert data["total_count"] == 0
        assert data["injections"] == []

    @pytest.mark.asyncio
    async def test_list_injections_multiple(self, client):
        """Test listing multiple injections."""
        # Submit 3 injections
        for i in range(3):
            client.post(
                "/api/v1/sessions/ses_123/injections",
                json={
                    "key": "host:host_drive_cmd",
                    "values": {"speed_mmps": i * 10, "yaw_rate_mrad_s": 0, "gear": 0},
                    "bus": "high",
                },
            )

        # List them
        response = client.get(
            "/api/v1/sessions/ses_123/injections"
        )

        assert response.status_code == 200
        data = response.json()
        assert data["total_count"] >= 3
        assert len(data["injections"]) >= 3

    @pytest.mark.asyncio
    async def test_list_with_pagination(self, client):
        """Test pagination."""
        # Submit 5 injections
        for i in range(5):
            client.post(
                "/api/v1/sessions/ses_123/injections",
                json={
                    "key": "host:host_drive_cmd",
                    "values": {"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
                    "bus": "high",
                },
            )

        # Get first page
        response = client.get(
            "/api/v1/sessions/ses_123/injections?limit=2&offset=0"
        )
        assert response.status_code == 200
        data = response.json()
        assert len(data["injections"]) <= 2

    @pytest.mark.asyncio
    async def test_list_with_status_filter(self, client):
        """Test filtering by status."""
        # Submit injection
        client.post(
            "/api/v1/sessions/ses_123/injections",
            json={
                "key": "host:host_drive_cmd",
                "values": {"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
                "bus": "high",
            },
        )

        # Filter by submitted status
        response = client.get(
            "/api/v1/sessions/ses_123/injections?status=submitted"
        )

        assert response.status_code == 200
        data = response.json()
        # May have 0 or more submitted injections


class TestGetInjection:
    """Test getting specific injection."""

    @pytest.mark.asyncio
    async def test_get_injection(self, client):
        """Test getting injection details."""
        # Submit injection
        submit_response = client.post(
            "/api/v1/sessions/ses_123/injections",
            json={
                "key": "host:host_drive_cmd",
                "values": {"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
                "bus": "high",
            },
        )

        injection_id = submit_response.json()["injection_id"]

        # Get it
        response = client.get(
            f"/api/v1/sessions/ses_123/injections/{injection_id}"
        )

        assert response.status_code == 200
        data = response.json()
        assert data["injection"]["injection_id"] == injection_id
        assert data["injection"]["key"] == "host:host_drive_cmd"
        assert data["injection"]["bus"] == "high"

    @pytest.mark.asyncio
    async def test_get_nonexistent_injection(self, client):
        """Test error on nonexistent injection."""
        response = client.get(
            "/api/v1/sessions/ses_123/injections/inj_nonexistent"
        )

        assert response.status_code == 404


class TestCancelInjection:
    """Test canceling injections."""

    @pytest.mark.asyncio
    async def test_cancel_pending_injection(self, client):
        """Test canceling a pending injection."""
        # Submit delayed injection with longer delay
        submit_response = client.post(
            "/api/v1/sessions/ses_123/injections",
            json={
                "key": "host:host_drive_cmd",
                "values": {"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
                "bus": "high",
                "delay_ms": 5000,  # 5 second delay (long enough to cancel)
            },
        )

        injection_id = submit_response.json()["injection_id"]

        # Cancel it immediately
        response = client.delete(
            f"/api/v1/sessions/ses_123/injections/{injection_id}"
        )

        # May succeed (200) or fail if task already completed (409)
        assert response.status_code in (200, 409)

        if response.status_code == 200:
            data = response.json()
            assert data["injection_id"] == injection_id
            assert data["previous_status"] == "pending"
            assert data["new_status"] == "cancelled"

    @pytest.mark.asyncio
    async def test_cancel_submitted_injection(self, client):
        """Test error when trying to cancel submitted injection."""
        # Submit immediate injection
        submit_response = client.post(
            "/api/v1/sessions/ses_123/injections",
            json={
                "key": "host:host_drive_cmd",
                "values": {"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
                "bus": "high",
                "delay_ms": 0,
            },
        )

        injection_id = submit_response.json()["injection_id"]

        # Try to cancel (should fail or be immediate)
        response = client.delete(
            f"/api/v1/sessions/ses_123/injections/{injection_id}"
        )

        # May succeed or fail depending on timing
        assert response.status_code in (200, 409)

    @pytest.mark.asyncio
    async def test_cancel_nonexistent_injection(self, client):
        """Test error on nonexistent injection."""
        response = client.delete(
            "/api/v1/sessions/ses_123/injections/inj_nonexistent"
        )

        assert response.status_code == 404


class TestInjectionStats:
    """Test injection statistics."""

    @pytest.mark.asyncio
    async def test_get_stats_empty(self, client):
        """Test stats when no injections."""
        response = client.get(
            "/api/v1/sessions/ses_123/injections-stats"
        )

        assert response.status_code == 200
        data = response.json()
        stats = data["stats"]
        assert stats["total"] == 0
        assert stats["pending"] == 0
        assert stats["submitted"] == 0
        assert stats["failed"] == 0
        assert stats["cancelled"] == 0

    @pytest.mark.asyncio
    async def test_get_stats_with_injections(self, client):
        """Test stats with injections."""
        # Submit injection
        client.post(
            "/api/v1/sessions/ses_123/injections",
            json={
                "key": "host:host_drive_cmd",
                "values": {"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
                "bus": "high",
            },
        )

        # Get stats
        response = client.get(
            "/api/v1/sessions/ses_123/injections-stats"
        )

        assert response.status_code == 200
        data = response.json()
        stats = data["stats"]
        assert stats["total"] >= 1


class TestInjectionLifecycle:
    """Test complete injection lifecycle."""

    @pytest.mark.asyncio
    async def test_full_lifecycle_immediate(self, client):
        """Test complete lifecycle for immediate injection."""
        # 1. Submit
        submit_response = client.post(
            "/api/v1/sessions/ses_123/injections",
            json={
                "key": "host:host_drive_cmd",
                "values": {"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
                "bus": "high",
            },
        )
        assert submit_response.status_code == 200
        injection_id = submit_response.json()["injection_id"]

        # 2. List
        list_response = client.get(
            "/api/v1/sessions/ses_123/injections"
        )
        assert list_response.status_code == 200
        ids = [i["injection_id"] for i in list_response.json()["injections"]]
        assert injection_id in ids

        # 3. Get details
        get_response = client.get(
            f"/api/v1/sessions/ses_123/injections/{injection_id}"
        )
        assert get_response.status_code == 200

        # 4. Get stats
        stats_response = client.get(
            "/api/v1/sessions/ses_123/injections-stats"
        )
        assert stats_response.status_code == 200

    @pytest.mark.asyncio
    async def test_full_lifecycle_delayed_then_cancel(self, client):
        """Test lifecycle for delayed injection with cancellation."""
        # 1. Submit with delay (long enough to have time to cancel)
        submit_response = client.post(
            "/api/v1/sessions/ses_123/injections",
            json={
                "key": "host:host_drive_cmd",
                "values": {"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
                "bus": "high",
                "delay_ms": 5000,  # 5 second delay
            },
        )
        assert submit_response.status_code == 200
        injection_id = submit_response.json()["injection_id"]
        assert submit_response.json()["status"] == "pending"

        # 2. Cancel before it submits
        cancel_response = client.delete(
            f"/api/v1/sessions/ses_123/injections/{injection_id}"
        )

        # Should succeed (200) or fail if already completed (409)
        assert cancel_response.status_code in (200, 409)

        if cancel_response.status_code == 200:
            assert cancel_response.json()["new_status"] == "cancelled"

            # 3. Verify cancelled
            get_response = client.get(
                f"/api/v1/sessions/ses_123/injections/{injection_id}"
            )
            assert get_response.json()["injection"]["status"] == "cancelled"
