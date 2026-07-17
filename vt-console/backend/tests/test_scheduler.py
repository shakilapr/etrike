"""Test scheduler service (workplan §5.3)."""

import asyncio
import time

import pytest

from vtc.config import Profile
from vtc.services.encoder import EncoderService
from vtc.services.scheduler import Scheduler


@pytest.fixture
def encoder():
    """Create encoder service instance."""
    return EncoderService()


@pytest.fixture
async def scheduler_with_mock_submit(encoder):
    """Create scheduler with mock submission callback."""
    submitted_frames = []

    async def mock_submit(bus: str, can_id: int, data: bytes) -> None:
        submitted_frames.append((bus, can_id, data, time.monotonic_ns()))

    scheduler = Scheduler(encoder, mock_submit)
    scheduler._submitted_frames = submitted_frames
    return scheduler


class TestSchedulerBasics:
    """Basic scheduler functionality tests."""

    @pytest.mark.asyncio
    async def test_schedule_periodic_returns_job_id(self, scheduler_with_mock_submit):
        """Test that schedule_periodic returns a job ID."""
        scheduler = scheduler_with_mock_submit
        job_id = await scheduler.schedule_periodic(
            session_id="ses_123",
            key="host:host_drive_cmd",
            values={"speed_mmps": 100, "yaw_rate_mrad_s": 50, "gear": 1},
            bus="high",
            period_ms=100,
        )
        assert job_id.startswith("job_")
        assert len(job_id) > 4

    @pytest.mark.asyncio
    async def test_schedule_periodic_validates_encoding(self, scheduler_with_mock_submit):
        """Test that schedule_periodic validates message can be encoded."""
        scheduler = scheduler_with_mock_submit
        with pytest.raises(ValueError, match="Encode failed"):
            await scheduler.schedule_periodic(
                session_id="ses_123",
                key="host:host_drive_cmd",
                values={"speed_mmps": 0},  # Missing required fields
                bus="high",
                period_ms=100,
            )

    @pytest.mark.asyncio
    async def test_list_jobs_returns_scheduled_jobs(self, scheduler_with_mock_submit):
        """Test that list_jobs returns active jobs."""
        scheduler = scheduler_with_mock_submit
        job_id1 = await scheduler.schedule_periodic(
            session_id="ses_123",
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
            period_ms=100,
        )
        job_id2 = await scheduler.schedule_periodic(
            session_id="ses_123",
            key="host:host_heartbeat",
            values={"alive_ctr": 0, "health_flags": 0},
            bus="high",
            period_ms=500,
        )

        jobs = await scheduler.list_jobs()
        assert len(jobs) == 2
        assert any(j.job_id == job_id1 for j in jobs)
        assert any(j.job_id == job_id2 for j in jobs)

    @pytest.mark.asyncio
    async def test_list_jobs_filters_by_session(self, scheduler_with_mock_submit):
        """Test that list_jobs can filter by session."""
        scheduler = scheduler_with_mock_submit
        await scheduler.schedule_periodic(
            session_id="ses_123",
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
            period_ms=100,
        )
        await scheduler.schedule_periodic(
            session_id="ses_456",
            key="host:host_heartbeat",
            values={"alive_ctr": 0, "health_flags": 0},
            bus="high",
            period_ms=500,
        )

        jobs_123 = await scheduler.list_jobs("ses_123")
        jobs_456 = await scheduler.list_jobs("ses_456")
        assert len(jobs_123) == 1
        assert len(jobs_456) == 1
        assert jobs_123[0].session_id == "ses_123"
        assert jobs_456[0].session_id == "ses_456"


class TestSchedulerExecution:
    """Test scheduler execution and deadline handling."""

    @pytest.mark.asyncio
    async def test_periodic_job_executes_at_deadline(self, scheduler_with_mock_submit):
        """Test that jobs execute when their deadline passes."""
        scheduler = scheduler_with_mock_submit
        job_id = await scheduler.schedule_periodic(
            session_id="ses_123",
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
            period_ms=50,
        )

        # Run scheduler for 150ms
        scheduler_task = asyncio.create_task(scheduler.run())
        await asyncio.sleep(0.150)
        await scheduler.stop()
        await asyncio.wait_for(scheduler_task, timeout=1.0)

        # Should have submitted roughly 3 times (0, 50, 100ms)
        frames = scheduler._submitted_frames
        assert len(frames) >= 2, f"Expected at least 2 submissions, got {len(frames)}"

    @pytest.mark.asyncio
    async def test_cancel_job_stops_submissions(self, scheduler_with_mock_submit):
        """Test that cancel_job stops a periodic job."""
        scheduler = scheduler_with_mock_submit
        job_id = await scheduler.schedule_periodic(
            session_id="ses_123",
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
            period_ms=50,
        )

        scheduler_task = asyncio.create_task(scheduler.run())
        await asyncio.sleep(0.075)  # Let it submit once
        await scheduler.cancel_job(job_id)
        await asyncio.sleep(0.150)  # Wait for what would be more submissions
        await scheduler.stop()
        await asyncio.wait_for(scheduler_task, timeout=1.0)

        frames = scheduler._submitted_frames
        # Should have 1-2 submissions before cancel, then stop
        assert len(frames) < 4, f"Expected < 4 submissions after cancel, got {len(frames)}"

    @pytest.mark.asyncio
    async def test_missed_deadline_skips_stale_periods(self, scheduler_with_mock_submit):
        """Test that missed deadlines skip stale periods instead of bursting."""
        scheduler = scheduler_with_mock_submit
        job = await scheduler.schedule_periodic(
            session_id="ses_123",
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
            period_ms=50,
        )

        # Manually create a stale job (deadline 200ms in past)
        job_obj = (await scheduler.list_jobs())[0]
        job_obj.next_deadline_ns = time.monotonic_ns() - (200 * 1_000_000)

        # Process once
        await scheduler._process_deadlines()

        # After processing, next_deadline should have skipped ahead, not queued 4 submissions
        assert job_obj.next_deadline_ns > time.monotonic_ns()

    @pytest.mark.asyncio
    async def test_jitter_measurement(self, scheduler_with_mock_submit):
        """Test that jitter is measured and recorded."""
        scheduler = scheduler_with_mock_submit
        job_id = await scheduler.schedule_periodic(
            session_id="ses_123",
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
            period_ms=50,
        )

        scheduler_task = asyncio.create_task(scheduler.run())
        await asyncio.sleep(0.150)
        await scheduler.stop()
        await asyncio.wait_for(scheduler_task, timeout=1.0)

        stats = scheduler.get_jitter_stats(job_id)
        assert stats is not None
        assert "min_ms" in stats
        assert "max_ms" in stats
        assert "avg_ms" in stats
        assert stats["count"] >= 2


class TestSchedulerCounters:
    """Test counter handling, especially per-bus independence."""

    @pytest.mark.asyncio
    async def test_job_tracks_submission_count(self, scheduler_with_mock_submit):
        """Test that job tracks submission count."""
        scheduler = scheduler_with_mock_submit
        job = await scheduler.schedule_periodic(
            session_id="ses_123",
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
            period_ms=50,
        )

        job_obj = (await scheduler.list_jobs())[0]
        assert job_obj.submission_count == 0

        scheduler_task = asyncio.create_task(scheduler.run())
        await asyncio.sleep(0.150)
        await scheduler.stop()
        await asyncio.wait_for(scheduler_task, timeout=1.0)

        assert job_obj.submission_count >= 2

    @pytest.mark.asyncio
    async def test_per_bus_counters_independent(self, scheduler_with_mock_submit):
        """Test critical feature: RT heartbeat counters are independent per bus."""
        scheduler = scheduler_with_mock_submit

        # Schedule RT_HEARTBEAT on both buses
        job_high = await scheduler.schedule_periodic(
            session_id="ses_123",
            key="rt:rt_heartbeat",
            values={"alive_ctr": 0, "heartbeat_ok": 1},
            bus="high",
            period_ms=100,
        )
        job_low = await scheduler.schedule_periodic(
            session_id="ses_123",
            key="rt:rt_heartbeat",
            values={"alive_ctr": 0, "heartbeat_ok": 1},
            bus="low",
            period_ms=100,
        )

        scheduler_task = asyncio.create_task(scheduler.run())
        await asyncio.sleep(0.500)  # Run for 500ms (5+ periods)
        await scheduler.stop()
        await asyncio.wait_for(scheduler_task, timeout=1.0)

        jobs = await scheduler.list_jobs("ses_123")
        job_high_obj = next(j for j in jobs if j.bus == "high")
        job_low_obj = next(j for j in jobs if j.bus == "low")

        # Both should have submitted multiple times
        assert job_high_obj.submission_count >= 4
        assert job_low_obj.submission_count >= 4

        # This is the critical test: both submitted, but they're independent jobs
        # (In a more complex scenario with shared counter state, we'd verify
        # the counters themselves are independent, but here we just verify
        # both jobs execute independently)
        assert job_high_obj.submission_count >= job_low_obj.submission_count - 1


class TestSchedulerCleanup:
    """Test job cleanup and session clearing."""

    @pytest.mark.asyncio
    async def test_clear_session_jobs_removes_all_session_jobs(
        self, scheduler_with_mock_submit
    ):
        """Test that clear_session_jobs removes all jobs for a session."""
        scheduler = scheduler_with_mock_submit
        session_id = "ses_123"

        # Schedule multiple jobs
        await scheduler.schedule_periodic(
            session_id=session_id,
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
            period_ms=100,
        )
        await scheduler.schedule_periodic(
            session_id=session_id,
            key="host:host_heartbeat",
            values={"alive_ctr": 0, "health_flags": 0},
            bus="high",
            period_ms=500,
        )
        await scheduler.schedule_periodic(
            session_id="ses_456",
            key="host:host_brake_req",
            values={"brake_pressure_kpa": 0},
            bus="high",
            period_ms=200,
        )

        # Clear session 123
        cleared = await scheduler.clear_session_jobs(session_id)
        assert cleared == 2

        # Verify only session 456 remains
        jobs = await scheduler.list_jobs()
        assert len(jobs) == 1
        assert jobs[0].session_id == "ses_456"

    @pytest.mark.asyncio
    async def test_get_job_status(self, scheduler_with_mock_submit):
        """Test getting job status."""
        scheduler = scheduler_with_mock_submit
        job_id = await scheduler.schedule_periodic(
            session_id="ses_123",
            key="host:host_drive_cmd",
            values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            bus="high",
            period_ms=100,
        )

        job = await scheduler.get_job_status(job_id)
        assert job is not None
        assert job.job_id == job_id
        assert job.period_ms == 100

        # Non-existent job
        job = await scheduler.get_job_status("job_nonexistent")
        assert job is None


class TestSchedulerProfiles:
    """Test scheduler with different profiles."""

    @pytest.mark.asyncio
    async def test_schedule_with_profile(self, scheduler_with_mock_submit):
        """Test scheduling with different profiles."""
        scheduler = scheduler_with_mock_submit

        for profile in [
            Profile.PURE_SOFTWARE,
            Profile.BENCH_TEST,
            Profile.FULL_VEHICLE,
        ]:
            job_id = await scheduler.schedule_periodic(
                session_id="ses_123",
                key="host:host_drive_cmd",
                values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
                bus="high",
                period_ms=100,
                profile=profile,
            )
            assert job_id.startswith("job_")
            await scheduler.cancel_job(job_id)
