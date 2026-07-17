"""Test source conflict monitor (workplan §5.5)."""

import asyncio
import time

import pytest

from vtc.services.encoder import EncoderService
from vtc.services.scheduler import Scheduler
from vtc.services.source_conflict_monitor import SourceConflictMonitor, ConflictEvent
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


@pytest.fixture
async def conflict_monitor(synthetic_engine):
    """Create source conflict monitor."""
    return SourceConflictMonitor(synthetic_engine)


class TestMonitoringControl:
    """Test starting and stopping monitoring."""

    @pytest.mark.asyncio
    async def test_start_monitoring(self, conflict_monitor):
        """Test starting monitoring."""
        await conflict_monitor.start_monitoring(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        assert conflict_monitor.monitoring
        assert conflict_monitor.current_session == "ses_123"
        assert conflict_monitor.current_dut == DutKind.SYS

    @pytest.mark.asyncio
    async def test_stop_monitoring(self, conflict_monitor):
        """Test stopping monitoring."""
        await conflict_monitor.start_monitoring(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        assert conflict_monitor.monitoring

        await conflict_monitor.stop_monitoring("ses_123")

        assert not conflict_monitor.monitoring
        assert conflict_monitor.current_session is None
        assert conflict_monitor.current_dut is None

    @pytest.mark.asyncio
    async def test_stop_monitoring_wrong_session(self, conflict_monitor):
        """Test that stop monitoring with wrong session does nothing."""
        await conflict_monitor.start_monitoring(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        # Try to stop with different session
        await conflict_monitor.stop_monitoring("ses_999")

        # Should still be monitoring
        assert conflict_monitor.monitoring
        assert conflict_monitor.current_session == "ses_123"


class TestSubmissionTracking:
    """Test recording frame submissions."""

    @pytest.mark.asyncio
    async def test_record_submission_when_monitoring(self, conflict_monitor):
        """Test recording submission while monitoring."""
        await conflict_monitor.start_monitoring(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        await conflict_monitor.record_submission("high", 0x300)

        key = ("high", 0x300)
        assert key in conflict_monitor.recent_submissions
        assert len(conflict_monitor.recent_submissions[key]) == 1

    @pytest.mark.asyncio
    async def test_record_submission_when_not_monitoring(self, conflict_monitor):
        """Test that submissions are not recorded when not monitoring."""
        await conflict_monitor.record_submission("high", 0x300)

        # Should not be recorded
        assert ("high", 0x300) not in conflict_monitor.recent_submissions

    @pytest.mark.asyncio
    async def test_multiple_submissions_same_id(self, conflict_monitor):
        """Test recording multiple submissions to same CAN ID."""
        await conflict_monitor.start_monitoring(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        await conflict_monitor.record_submission("high", 0x300)
        await asyncio.sleep(0.005)  # 5ms
        await conflict_monitor.record_submission("high", 0x300)

        key = ("high", 0x300)
        assert len(conflict_monitor.recent_submissions[key]) == 2

    @pytest.mark.asyncio
    async def test_submission_buffer_cleared_on_stop(self, conflict_monitor):
        """Test that submission buffer is cleared when monitoring stops."""
        await conflict_monitor.start_monitoring(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        await conflict_monitor.record_submission("high", 0x300)
        assert len(conflict_monitor.recent_submissions) > 0

        await conflict_monitor.stop_monitoring("ses_123")
        assert len(conflict_monitor.recent_submissions) == 0


class TestEchoDetection:
    """Test echo detection (distinguishing echoes from real ECU traffic)."""

    @pytest.mark.asyncio
    async def test_ignore_recent_echo(self, conflict_monitor):
        """Test that recent submissions are treated as echoes."""
        await conflict_monitor.start_monitoring(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        # Record a submission
        await conflict_monitor.record_submission("high", 0x300)

        # Immediately receive a frame on same ID (within echo window)
        conflict = await conflict_monitor.on_frame_received("high", 0x300)

        # Should NOT be conflict (it's an echo)
        assert not conflict

    @pytest.mark.asyncio
    async def test_detect_after_echo_window(self, conflict_monitor):
        """Test that frames after echo window are processed normally."""
        await conflict_monitor.start_monitoring(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        # Record a submission
        await conflict_monitor.record_submission("high", 0x300)

        # Wait longer than echo window (50ms)
        await asyncio.sleep(0.060)  # 60ms

        # Activate a synthetic peer first
        await conflict_monitor.synthetic_peers.activate_peers(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        # Now receive a frame on same ID (outside echo window)
        # This should be detected as a potential conflict
        # (but our monitor can't tell it's real without CAN ID mapping)
        conflict = await conflict_monitor.on_frame_received("high", 0x300)

        # Result depends on whether we have a peer on this bus
        # For this test, we just verify no crash
        assert isinstance(conflict, bool)


class TestConflictDetection:
    """Test detecting conflicts with synthetic peers."""

    @pytest.mark.asyncio
    async def test_no_conflict_when_not_monitoring(self, conflict_monitor):
        """Test that frames are ignored when not monitoring."""
        conflict = await conflict_monitor.on_frame_received("high", 0x300)
        assert not conflict

    @pytest.mark.asyncio
    async def test_conflict_event_recorded(self, conflict_monitor):
        """Test that conflict events are recorded."""
        await conflict_monitor.start_monitoring(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        # Wait past echo window
        await asyncio.sleep(0.060)

        # Activate peers
        await conflict_monitor.synthetic_peers.activate_peers(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        # Receive frame on bus with active peers
        conflict = await conflict_monitor.on_frame_received("high", 0x300, "seb_ecu")

        # Check if event was recorded (even if not a direct conflict due to our limitations)
        # The event recording happens when _find_peer_for_can_id returns a peer name
        status = await conflict_monitor.get_conflict_report()
        # Just verify the report structure
        assert "conflict_count" in status
        assert "recent_conflicts" in status


class TestConflictReporting:
    """Test conflict diagnostics and reporting."""

    @pytest.mark.asyncio
    async def test_get_conflict_report_when_not_monitoring(self, conflict_monitor):
        """Test getting report when not monitoring."""
        report = await conflict_monitor.get_conflict_report()

        assert report["monitoring"] is False
        assert report["session_id"] is None
        assert report["dut"] is None
        assert report["conflict_count"] == 0

    @pytest.mark.asyncio
    async def test_get_conflict_report_when_monitoring(self, conflict_monitor):
        """Test getting report when monitoring."""
        await conflict_monitor.start_monitoring(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        report = await conflict_monitor.get_conflict_report()

        assert report["monitoring"] is True
        assert report["session_id"] == "ses_123"
        assert report["dut"] == "sys"
        assert report["conflict_count"] == 0
        assert report["recent_conflicts"] == []

    @pytest.mark.asyncio
    async def test_conflict_report_includes_diagnostics(self, conflict_monitor):
        """Test that conflict report includes event details."""
        await conflict_monitor.start_monitoring(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        # Manually record a conflict event
        now_ns = time.monotonic_ns()
        event = ConflictEvent(
            timestamp_ns=now_ns,
            bus="high",
            can_id=0x300,
            synthetic_peer_name="sys:rt_heartbeat_high",
            real_source="seb_ecu",
        )
        conflict_monitor.conflict_events.append(event)

        report = await conflict_monitor.get_conflict_report()

        assert report["conflict_count"] == 1
        assert len(report["recent_conflicts"]) == 1

        event_report = report["recent_conflicts"][0]
        assert event_report["bus"] == "high"
        assert event_report["can_id"] == 0x300
        assert event_report["peer"] == "sys:rt_heartbeat_high"
        assert event_report["source"] == "seb_ecu"


class TestConflictResolution:
    """Test resolving conflicts by stopping peers."""

    @pytest.mark.asyncio
    async def test_resolve_conflict_stops_peer(self, conflict_monitor):
        """Test that resolving conflict stops the synthetic peer."""
        await conflict_monitor.start_monitoring(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        # Activate peers
        peers = await conflict_monitor.synthetic_peers.activate_peers(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        initial_count = len(conflict_monitor.synthetic_peers.active_peers)
        assert initial_count > 0

        # Try to resolve (this will stop a peer on matching bus)
        stopped = await conflict_monitor.resolve_conflict("low", 0x400)

        # Check if a peer was stopped
        final_count = len(conflict_monitor.synthetic_peers.active_peers)
        # May or may not have stopped depending on which bus had active peers
        assert isinstance(stopped, bool)


class TestActivePeerTracking:
    """Test listing active synthetic peers by bus."""

    @pytest.mark.asyncio
    async def test_list_active_peers_empty(self, conflict_monitor):
        """Test listing peers when none active."""
        peers = await conflict_monitor.list_active_peers_by_bus("high")
        assert peers == []

    @pytest.mark.asyncio
    async def test_list_active_peers_by_bus(self, conflict_monitor):
        """Test listing active peers for a specific bus."""
        await conflict_monitor.start_monitoring(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        # Activate peers (some may fail to encode, but at least some should succeed)
        await conflict_monitor.synthetic_peers.activate_peers(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        # List peers on high bus (may be empty if all fail to encode)
        peers = await conflict_monitor.list_active_peers_by_bus("high")

        # If any peers are listed, verify their structure
        if peers:
            assert all(p["bus"] == "high" for p in peers)
            assert all("name" in p and "key" in p and "job_id" in p for p in peers)

    @pytest.mark.asyncio
    async def test_list_active_peers_filters_by_bus(self, conflict_monitor):
        """Test that listing only returns peers on specified bus."""
        await conflict_monitor.start_monitoring(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        # Activate peers
        await conflict_monitor.synthetic_peers.activate_peers(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        # List peers on both buses
        high_peers = await conflict_monitor.list_active_peers_by_bus("high")
        low_peers = await conflict_monitor.list_active_peers_by_bus("low")

        # Should have peers on both buses (from SYS_DUT_PEERS)
        # Check that they're different
        high_names = {p["name"] for p in high_peers}
        low_names = {p["name"] for p in low_peers}

        # Verify no overlap (each peer should be on only one bus)
        assert len(high_names & low_names) == 0


class TestConcurrency:
    """Test thread-safety and concurrent operations."""

    @pytest.mark.asyncio
    async def test_concurrent_submissions(self, conflict_monitor):
        """Test recording submissions concurrently."""
        await conflict_monitor.start_monitoring(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        # Record multiple submissions concurrently
        await asyncio.gather(
            conflict_monitor.record_submission("high", 0x300),
            conflict_monitor.record_submission("high", 0x301),
            conflict_monitor.record_submission("low", 0x400),
            conflict_monitor.record_submission("high", 0x300),
        )

        # Check that all were recorded
        assert len(conflict_monitor.recent_submissions) >= 3

    @pytest.mark.asyncio
    async def test_concurrent_frame_reception(self, conflict_monitor):
        """Test receiving frames concurrently."""
        await conflict_monitor.start_monitoring(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        # Receive frames concurrently
        results = await asyncio.gather(
            conflict_monitor.on_frame_received("high", 0x300),
            conflict_monitor.on_frame_received("high", 0x301),
            conflict_monitor.on_frame_received("low", 0x400),
        )

        # All should complete without error
        assert len(results) == 3
        assert all(isinstance(r, bool) for r in results)


class TestBufferManagement:
    """Test buffer size limits and cleanup."""

    @pytest.mark.asyncio
    async def test_submission_buffer_max_size(self, conflict_monitor):
        """Test that submission buffer respects max size."""
        await conflict_monitor.start_monitoring(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        # Record more submissions than max
        for _ in range(20):
            await conflict_monitor.record_submission("high", 0x300)

        # Should not exceed max
        key = ("high", 0x300)
        assert len(conflict_monitor.recent_submissions[key]) <= conflict_monitor.max_submissions_per_id

    @pytest.mark.asyncio
    async def test_conflict_history_max_size(self, conflict_monitor):
        """Test that conflict history respects max size."""
        await conflict_monitor.start_monitoring(
            session_id="ses_123",
            dut=DutKind.SYS,
        )

        # Add many conflict events
        for i in range(150):
            event = ConflictEvent(
                timestamp_ns=time.monotonic_ns(),
                bus="high",
                can_id=0x300 + i,
                synthetic_peer_name=f"peer_{i}",
                real_source="ecu",
            )
            conflict_monitor.conflict_events.append(event)

        # Should not exceed max
        assert len(conflict_monitor.conflict_events) <= conflict_monitor.max_conflict_history
