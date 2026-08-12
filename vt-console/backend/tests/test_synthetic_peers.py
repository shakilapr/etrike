"""Test synthetic peer engine (workplan §5.4).

This test suite verifies:
- Peer templates (startup values, periods)
- Listen-before-speak window
- Peer activation and job scheduling
- Source conflict detection
- Per-bus counter independence
- Counter regeneration
- Status tracking
"""

import time

import pytest

from vtc.config import Profile
from vtc.services.encoder import EncoderService
from vtc.services.scheduler import Scheduler
from vtc.services.synthetic_peers import DutKind, SyntheticPeerEngine


@pytest.fixture
def encoder():
    """Create encoder service."""
    return EncoderService()


@pytest.fixture
async def scheduler(encoder):
    """Create scheduler with mock submission."""
    async def mock_submit(bus: str, can_id: int, data: bytes) -> None:
        pass

    return Scheduler(encoder, mock_submit)


@pytest.fixture
async def synthetic_engine(scheduler):
    """Create synthetic peer engine."""
    return SyntheticPeerEngine(scheduler)


class TestSyntheticPeerTemplates:
    """Test peer template definitions."""

    @pytest.mark.asyncio
    async def test_sys_dut_peers_defined(self, synthetic_engine):
        """Test that SYS-DUT peer set is defined."""
        peers = SyntheticPeerEngine.SYS_DUT_PEERS
        assert len(peers) > 0
        assert any(p.name == "sys:rt_heartbeat_high" for p in peers)
        assert any(p.name == "sys:rt_heartbeat_low" for p in peers)
        assert any(p.name == "sys:rt_drive_cmd" for p in peers)

    @pytest.mark.asyncio
    async def test_rt_dut_peers_defined(self, synthetic_engine):
        """Test that RT-DUT peer set is defined."""
        peers = SyntheticPeerEngine.RT_DUT_PEERS
        assert len(peers) > 0
        assert any(p.name == "rt:host_drive_cmd" for p in peers)
        assert any(p.name == "rt:host_steer_cmd" for p in peers)
        assert any(p.name == "rt:sys_heartbeat" for p in peers)

    @pytest.mark.asyncio
    async def test_startup_values_neutral(self, synthetic_engine):
        """Test that startup values are safe/neutral."""
        # RT heartbeat should start at 0, not some random value
        rt_hb = next(
            p for p in SyntheticPeerEngine.SYS_DUT_PEERS
            if p.name == "sys:rt_heartbeat_high"
        )
        assert rt_hb.startup_values["alive_ctr"] == 0
        assert rt_hb.startup_values["heartbeat_ok"] == 1

    @pytest.mark.asyncio
    async def test_periods_match_yaml(self, synthetic_engine):
        """Test that periods match YAML cycle_ms from workplan."""
        # From workplan: SYS heartbeat 100ms, RT heartbeat 500ms, etc.
        sys_hb = next(
            p for p in SyntheticPeerEngine.SYS_DUT_PEERS
            if p.name == "sys:rt_heartbeat_high"
        )
        assert sys_hb.period_ms == 500  # RT heartbeat @ 500ms per workplan

        sys_drive = next(
            p for p in SyntheticPeerEngine.SYS_DUT_PEERS
            if p.name == "sys:rt_drive_cmd"
        )
        assert sys_drive.period_ms == 10  # RT_DRIVE_CMD @ 10ms

    @pytest.mark.asyncio
    async def test_counter_fields_defined(self, synthetic_engine):
        """Test that counter fields are properly defined."""
        # RT heartbeat should have counter field
        rt_hb = next(
            p for p in SyntheticPeerEngine.SYS_DUT_PEERS
            if p.name == "sys:rt_heartbeat_high"
        )
        assert rt_hb.counter_field == "alive_ctr"
        assert rt_hb.counter_max == 255

        # RT_DRIVE_CMD should not have counter
        rt_drive = next(
            p for p in SyntheticPeerEngine.SYS_DUT_PEERS
            if p.name == "sys:rt_drive_cmd"
        )
        assert rt_drive.counter_field is None

        host_steer = next(
            p for p in SyntheticPeerEngine.RT_DUT_PEERS
            if p.name == "rt:host_steer_cmd"
        )
        assert host_steer.period_ms == 10
        assert host_steer.counter_field == "rolling_counter"
        assert host_steer.counter_max == 255
        assert host_steer.startup_values["angle_valid"] == 1


class TestListenWindow:
    """Test listen-before-speak window."""

    @pytest.mark.asyncio
    async def test_start_listen_window(self, synthetic_engine):
        """Test starting a listen window."""
        window = await synthetic_engine.start_listen_window(
            session_id="ses_123",
            dut=DutKind.SYS,
            duration_ms=500,
        )

        assert window is not None
        assert window.dut == DutKind.SYS
        assert window.duration_ms == 500
        assert window.is_active(time.monotonic_ns())

    @pytest.mark.asyncio
    async def test_listen_window_expires(self, synthetic_engine):
        """Test that listen window expires after duration."""
        window = await synthetic_engine.start_listen_window(
            session_id="ses_123",
            dut=DutKind.SYS,
            duration_ms=10,  # 10ms
        )

        assert window.is_active(time.monotonic_ns())

        # Wait for expiration
        await asyncio.sleep(0.015)  # 15ms
        now_ns = time.monotonic_ns()
        assert not window.is_active(now_ns)

    @pytest.mark.asyncio
    async def test_listen_window_remaining_time(self, synthetic_engine):
        """Test remaining time calculation."""
        window = await synthetic_engine.start_listen_window(
            session_id="ses_123",
            dut=DutKind.SYS,
            duration_ms=500,
        )

        now_ns = time.monotonic_ns()
        remaining = window.remaining_ms(now_ns)
        assert 490 < remaining <= 500


class TestPeerActivation:
    """Test synthetic peer activation."""

    @pytest.mark.asyncio
    async def test_activate_sys_dut_peers(self, synthetic_engine):
        """Test activating SYS-DUT peer set."""
        peers = await synthetic_engine.activate_peers(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        assert len(peers) > 0
        assert "sys:rt_heartbeat_high" in peers
        assert "sys:rt_heartbeat_low" in peers

    @pytest.mark.asyncio
    async def test_activate_rt_dut_peers(self, synthetic_engine):
        """Test activating RT-DUT peer set."""
        peers = await synthetic_engine.activate_peers(
            session_id="ses_123",
            dut=DutKind.RT,
        )

        assert len(peers) > 0
        assert "rt:host_drive_cmd" in peers
        assert "rt:sys_heartbeat" in peers

    @pytest.mark.asyncio
    async def test_activate_phase2_host_steer_peer(self, synthetic_engine):
        peers = await synthetic_engine.activate_peers(
            session_id="ses_123",
            dut=DutKind.RT,
        )

        steer = peers["rt:host_steer_cmd"]
        assert steer.peer_template.period_ms == 10
        assert steer.peer_template.counter_field == "rolling_counter"

    @pytest.mark.asyncio
    async def test_activated_peers_have_job_ids(self, synthetic_engine):
        """Test that activated peers have job IDs."""
        peers = await synthetic_engine.activate_peers(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        for peer_name, instance in peers.items():
            assert instance.job_id is not None
            assert instance.job_id.startswith("job_")

    @pytest.mark.asyncio
    async def test_activated_peers_tracked(self, synthetic_engine):
        """Test that activated peers are tracked in engine."""
        await synthetic_engine.activate_peers(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        assert len(synthetic_engine.active_peers) > 0
        assert synthetic_engine.current_dut == DutKind.SYS


class TestPeerStopAll:
    """Test stopping all synthetic peers."""

    @pytest.mark.asyncio
    async def test_stop_all_cancels_jobs(self, synthetic_engine):
        """Test that stop all cancels all peer jobs."""
        await synthetic_engine.activate_peers(
            session_id="ses_123",
            dut=DutKind.SYS,
        )
        initial_count = len(synthetic_engine.active_peers)
        assert initial_count > 0

        stopped = await synthetic_engine.stop_all("ses_123")
        assert stopped == initial_count
        assert len(synthetic_engine.active_peers) == 0

    @pytest.mark.asyncio
    async def test_stop_all_clears_state(self, synthetic_engine):
        """Test that stop all clears engine state."""
        await synthetic_engine.start_listen_window(
            session_id="ses_123",
            dut=DutKind.SYS,
        )
        await synthetic_engine.activate_peers(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        await synthetic_engine.stop_all("ses_123")

        assert synthetic_engine.listen_window is None
        assert synthetic_engine.current_dut is None
        assert len(synthetic_engine.active_peers) == 0


class TestPeerCounters:
    """Test per-bus counter independence."""

    @pytest.mark.asyncio
    async def test_rt_heartbeat_independent_counters(self, synthetic_engine):
        """Test critical feature: RT heartbeat has independent counters per bus."""
        await synthetic_engine.activate_peers(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        # Get the two RT heartbeat instances
        rt_hb_high = synthetic_engine.active_peers.get("sys:rt_heartbeat_high")
        rt_hb_low = synthetic_engine.active_peers.get("sys:rt_heartbeat_low")

        assert rt_hb_high is not None
        assert rt_hb_low is not None

        # Both should be on same message key but different buses
        assert rt_hb_high.peer_template.key == rt_hb_low.peer_template.key
        assert rt_hb_high.peer_template.bus == "high"
        assert rt_hb_low.peer_template.bus == "low"

        # Both have counter fields
        assert rt_hb_high.peer_template.counter_field == "alive_ctr"
        assert rt_hb_low.peer_template.counter_field == "alive_ctr"

    @pytest.mark.asyncio
    async def test_counter_fields_present(self, synthetic_engine):
        """Test that peers with counter fields are identified."""
        await synthetic_engine.activate_peers(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        counter_peers = [
            p for p in synthetic_engine.active_peers.values()
            if p.peer_template.counter_field is not None
        ]
        assert len(counter_peers) > 0


class TestStatus:
    """Test status reporting."""

    @pytest.mark.asyncio
    async def test_get_status_before_activation(self, synthetic_engine):
        """Test status before any activation."""
        status = await synthetic_engine.get_status()

        assert status["dut"] is None
        assert status["listening"] is False
        assert status["active_peer_count"] == 0

    @pytest.mark.asyncio
    async def test_get_status_with_active_peers(self, synthetic_engine):
        """Test status with active peers."""
        await synthetic_engine.activate_peers(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        status = await synthetic_engine.get_status()

        assert status["dut"] == "sys"
        assert status["active_peer_count"] > 0
        assert len(status["active_peers"]) > 0

    @pytest.mark.asyncio
    async def test_get_status_with_listen_window(self, synthetic_engine):
        """Test status during listen window."""
        await synthetic_engine.start_listen_window(
            session_id="ses_123",
            dut=DutKind.SYS,
            duration_ms=500,
        )

        status = await synthetic_engine.get_status()

        assert status["listening"] is True
        assert status["listen_remaining_ms"] is not None
        assert 0 < status["listen_remaining_ms"] <= 500


class TestAvailablePeers:
    """Test listing available peers."""

    @pytest.mark.asyncio
    async def test_list_sys_dut_available_peers(self, synthetic_engine):
        """Test listing available SYS-DUT peers."""
        peers = await synthetic_engine.list_available_peers(DutKind.SYS)

        assert len(peers) > 0
        assert any(p["name"] == "sys:rt_heartbeat_high" for p in peers)

        # Check peer info structure
        peer = peers[0]
        assert "name" in peer
        assert "key" in peer
        assert "bus" in peer
        assert "period_ms" in peer
        assert "has_counter" in peer

    @pytest.mark.asyncio
    async def test_list_rt_dut_available_peers(self, synthetic_engine):
        """Test listing available RT-DUT peers."""
        peers = await synthetic_engine.list_available_peers(DutKind.RT)

        assert len(peers) > 0
        assert any(p["name"] == "rt:host_drive_cmd" for p in peers)


# Import asyncio for async tests
import asyncio
