"""Test TX gate service (workplan §5.2)."""

import pytest

from vtc.config import Profile
from vtc.models.session import BenchTxState, SessionState
from vtc.services.encoder import EncoderService
from vtc.services.ownership import OwnershipTable, Lease
from vtc.services.tx_gate import TxGate


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
def session_state_bench_enabled():
    """Create session state with Bench TX enabled."""
    state = SessionState()
    state.profile = Profile.PURE_SOFTWARE
    state.bench_tx = BenchTxState.ENABLED
    state.session_id = "ses_123"
    return state


class TestTxGateAcceptance:
    """Test acceptance when all guards pass."""

    @pytest.mark.asyncio
    async def test_accept_when_all_guards_pass(self, tx_gate, ownership_table, session_state_bench_enabled):
        """Test that TX is accepted when all guards pass."""
        # Add ownership lease (claim will create it)
        ownership_table.claim(
            bus="high",
            can_id=0x300,
            owner="ses_123",
            ttl_s=10.0,
        )

        result = await tx_gate.submit_for_transmission(
            session_state=session_state_bench_enabled,
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
            session_id="ses_123",
            owner="ses_123",
        )

        assert result.ok
        assert result.disposition == "submitted"
        assert result.can_id == 0x300
        assert result.data is not None
        assert result.status_code is None  # No error

    @pytest.mark.asyncio
    async def test_accept_returns_encoded_data(self, tx_gate, ownership_table, session_state_bench_enabled):
        """Test that accepted submission returns encoded data."""
        ownership_table.claim(
            bus="high",
            can_id=0x300,
            owner="ses_123",
            ttl_s=10.0,
        )

        result = await tx_gate.submit_for_transmission(
            session_state=session_state_bench_enabled,
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
            session_id="ses_123",
            owner="ses_123",
        )

        assert result.ok
        assert isinstance(result.data, bytes)
        assert len(result.data) == 8  # HOST_DRIVE_CMD DLC


class TestTxGateBenchTxGuard:
    """Test Bench TX disabled guard."""

    @pytest.mark.asyncio
    async def test_reject_bench_tx_disabled(self, tx_gate, ownership_table):
        """Test rejection when Bench TX is disabled."""
        state = SessionState()
        state.profile = Profile.PURE_SOFTWARE
        state.bench_tx = BenchTxState.DISABLED  # Disabled
        state.session_id = "ses_123"

        ownership_table.claim(
            bus="high",
            can_id=0x300,
            owner="ses_123",
            ttl_s=10.0,
        )

        result = await tx_gate.submit_for_transmission(
            session_state=state,
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
            session_id="ses_123",
            owner="ses_123",
        )

        assert not result.ok
        assert result.error_code == "bench_tx.disabled"
        assert result.status_code == 503


class TestTxGateProfileGuard:
    """Test profile guard."""

    @pytest.mark.asyncio
    async def test_reject_physical_profile(self, tx_gate, ownership_table, session_state_bench_enabled):
        """Test rejection when profile is physical."""
        session_state_bench_enabled.profile = Profile.BENCH_TEST

        ownership_table.claim(
            bus="high",
            can_id=0x300,
            owner="ses_123",
            ttl_s=10.0,
        )

        result = await tx_gate.submit_for_transmission(
            session_state=session_state_bench_enabled,
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
            session_id="ses_123",
            owner="ses_123",
        )

        assert not result.ok
        assert result.error_code == "profile.tx_not_permitted"
        assert result.status_code == 403

    @pytest.mark.asyncio
    async def test_reject_full_vehicle_profile(self, tx_gate, ownership_table, session_state_bench_enabled):
        """Test rejection for Full Vehicle profile."""
        session_state_bench_enabled.profile = Profile.FULL_VEHICLE

        ownership_table.claim(
            bus="high",
            can_id=0x300,
            owner="ses_123",
            ttl_s=10.0,
        )

        result = await tx_gate.submit_for_transmission(
            session_state=session_state_bench_enabled,
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
            session_id="ses_123",
            owner="ses_123",
        )

        assert not result.ok
        assert result.error_code == "profile.tx_not_permitted"
        assert result.status_code == 403


class TestTxGateOwnershipGuard:
    """Test ownership/lease guards."""

    @pytest.mark.asyncio
    async def test_reject_no_lease(self, tx_gate, session_state_bench_enabled):
        """Test rejection when source has no lease."""
        result = await tx_gate.submit_for_transmission(
            session_state=session_state_bench_enabled,
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
            session_id="ses_123",
            owner="ses_123",
        )

        assert not result.ok
        assert result.error_code == "ownership.no_lease"
        assert result.status_code == 410

    @pytest.mark.asyncio
    async def test_reject_ownership_conflict(self, tx_gate, ownership_table, session_state_bench_enabled):
        """Test rejection when different owner has the lease."""
        # Someone else owns it
        ownership_table.claim_ownership(
            bus="high",
            can_id=0x300,
            owner="other_owner",
            ttl_ms=10000,
        )

        result = await tx_gate.submit_for_transmission(
            session_state=session_state_bench_enabled,
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
            session_id="ses_123",
            owner="ses_123",
        )

        assert not result.ok
        assert result.error_code == "ownership.conflict"
        assert result.status_code == 409

    @pytest.mark.asyncio
    async def test_reject_expired_lease(self, tx_gate, ownership_table, session_state_bench_enabled):
        """Test rejection when lease is expired."""
        # Claim but with 0ms TTL (immediately expired)
        lease = ownership_table.claim_ownership(
            bus="high",
            can_id=0x300,
            owner="ses_123",
            ttl_ms=0,  # Expired immediately
        )

        # Give it a moment to ensure expiration
        import time
        time.sleep(0.01)

        result = await tx_gate.submit_for_transmission(
            session_state=session_state_bench_enabled,
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
            session_id="ses_123",
            owner="ses_123",
        )

        assert not result.ok
        assert result.error_code == "lease.expired"
        assert result.status_code == 410


class TestTxGateEncoderGuard:
    """Test encoder validation guard."""

    @pytest.mark.asyncio
    async def test_reject_invalid_encoding(self, tx_gate, ownership_table, session_state_bench_enabled):
        """Test rejection when message cannot be encoded."""
        ownership_table.claim(
            bus="high",
            can_id=0x300,
            owner="ses_123",
            ttl_s=10.0,
        )

        result = await tx_gate.submit_for_transmission(
            session_state=session_state_bench_enabled,
            key="host:host_drive_cmd",
            values={"speed_mmps": 0},  # Missing required fields
            bus="high",
            session_id="ses_123",
            owner="ses_123",
        )

        assert not result.ok
        assert "encode." in result.error_code
        assert result.status_code == 422

    @pytest.mark.asyncio
    async def test_reject_out_of_range_values(self, tx_gate, ownership_table, session_state_bench_enabled):
        """Test rejection for out-of-range values."""
        ownership_table.claim(
            bus="high",
            can_id=0x300,
            owner="ses_123",
            ttl_s=10.0,
        )

        result = await tx_gate.submit_for_transmission(
            session_state=session_state_bench_enabled,
            key="host:host_drive_cmd",
            values={
                "speed_mmps": 10000,  # Out of range
                "yaw_rate_mrad_s": 0,
                "gear": 0,
            },
            bus="high",
            session_id="ses_123",
            owner="ses_123",
        )

        assert not result.ok
        assert "encode." in result.error_code
        assert result.status_code == 422


class TestTxGateQuickCheck:
    """Test quick guardian check without encoding."""

    @pytest.mark.asyncio
    async def test_check_guardians_pass(self, tx_gate, ownership_table, session_state_bench_enabled):
        """Test quick guardian check succeeds when all pass."""
        ownership_table.claim(
            bus="high",
            can_id=0x300,
            owner="ses_123",
            ttl_s=10.0,
        )

        ok, error_code, status_code = await tx_gate.check_guardians(
            session_state=session_state_bench_enabled,
            bus="high",
            can_id=0x300,
            owner="ses_123",
        )

        assert ok
        assert error_code is None
        assert status_code is None

    @pytest.mark.asyncio
    async def test_check_guardians_bench_tx_disabled(self, tx_gate, ownership_table):
        """Test quick check detects Bench TX disabled."""
        state = SessionState()
        state.bench_tx = BenchTxState.DISABLED

        ownership_table.claim(
            bus="high",
            can_id=0x300,
            owner="ses_123",
            ttl_s=10.0,
        )

        ok, error_code, status_code = await tx_gate.check_guardians(
            session_state=state,
            bus="high",
            can_id=0x300,
            owner="ses_123",
        )

        assert not ok
        assert error_code == "bench_tx.disabled"
        assert status_code == 503


class TestTxGateJobTypes:
    """Test different job types."""

    @pytest.mark.asyncio
    async def test_one_shot_job_type(self, tx_gate, ownership_table, session_state_bench_enabled):
        """Test one-shot job type."""
        ownership_table.claim(
            bus="high",
            can_id=0x300,
            owner="ses_123",
            ttl_s=10.0,
        )

        result = await tx_gate.submit_for_transmission(
            session_state=session_state_bench_enabled,
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
            session_id="ses_123",
            owner="ses_123",
            job_type="one_shot",
        )

        assert result.ok

    @pytest.mark.asyncio
    async def test_periodic_job_type(self, tx_gate, ownership_table, session_state_bench_enabled):
        """Test periodic job type."""
        ownership_table.claim(
            bus="high",
            can_id=0x300,
            owner="ses_123",
            ttl_s=10.0,
        )

        result = await tx_gate.submit_for_transmission(
            session_state=session_state_bench_enabled,
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
            session_id="ses_123",
            owner="ses_123",
            job_type="periodic",
            period_ms=100,
        )

        assert result.ok
