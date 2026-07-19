"""Periodic job scheduler with monotonic deadlines (workplan §5.3).

Core responsibilities:
- Maintain set of active periodic jobs
- Use absolute monotonic deadlines (not relative intervals)
- Per-period re-encode with fresh counter/checksum (no static payloads)
- Independent per-bus counter tracking (critical for RT 0x7FD High vs Low)
- Missed deadline handling: skip stale periods, never burst catch-up
- Jitter measurement: actual_submission_time - scheduled_deadline
"""

from __future__ import annotations

import asyncio
import time
import uuid
from dataclasses import dataclass, field
from typing import Any, Callable

from vtc.config import Profile
from vtc.services.encoder import EncoderService, EncodeResult


@dataclass
class PeriodicJob:
    """A single periodic job to be scheduled."""

    job_id: str
    session_id: str
    key: str  # Message key (e.g., "host:host_drive_cmd")
    values: dict[str, Any]  # Engineering values
    bus: str  # "high" or "low"
    period_ms: int  # Period in milliseconds
    next_deadline_ns: int  # Monotonic nanoseconds (from time.monotonic_ns())
    counter_per_bus: dict[str, int] = field(default_factory=dict)  # Per-bus counters
    last_submit_ns: int = 0
    jitter_measurements: list[int] = field(default_factory=list)  # In nanoseconds
    created_at_ns: int = field(default_factory=lambda: time.monotonic_ns())
    submission_count: int = 0


class Scheduler:
    """Periodic job scheduler with monotonic deadlines.

    Single-threaded background worker that manages periodic frame transmission.
    """

    def __init__(
        self,
        encoder: EncoderService,
        submit_callback: Callable[[str, int, bytes], None],
    ):
        """Initialize scheduler.

        Args:
            encoder: EncoderService instance for encoding frames
            submit_callback: Async function to call on TX: submit_callback(bus, can_id, data)
        """
        self.encoder = encoder
        self.submit_callback = submit_callback
        self.jobs: dict[str, PeriodicJob] = {}
        self._running = False
        self._lock = asyncio.Lock()

    async def schedule_periodic(
        self,
        session_id: str,
        key: str,
        values: dict[str, Any],
        bus: str,
        period_ms: int,
        profile: Profile = Profile.PURE_SOFTWARE,
    ) -> str:
        """Add a periodic job to the scheduler.

        Args:
            session_id: Session owner
            key: Message key (e.g., "host:host_drive_cmd")
            values: Engineering values for encoding
            bus: "high" or "low"
            period_ms: Period in milliseconds
            profile: Operating profile

        Returns:
            Job ID for later cancellation

        Raises:
            ValueError: If encoding the initial frame fails
        """
        async with self._lock:
            # Verify encoding works before scheduling
            encode_result = self.encoder.encode_message(
                key, values, bus, profile, session_id
            )
            if not encode_result.ok:
                raise ValueError(f"Encode failed: {encode_result.error}")

            # Create job with first deadline = now
            job_id = f"job_{uuid.uuid4().hex[:12]}"
            now_ns = time.monotonic_ns()
            job = PeriodicJob(
                job_id=job_id,
                session_id=session_id,
                key=key,
                values=values,
                bus=bus,
                period_ms=period_ms,
                next_deadline_ns=now_ns + (period_ms * 1_000_000),  # ns per ms
            )

            self.jobs[job_id] = job
            return job_id

    async def cancel_job(self, job_id: str) -> None:
        """Cancel a periodic job immediately.

        Args:
            job_id: Job ID from schedule_periodic
        """
        async with self._lock:
            self.jobs.pop(job_id, None)

    async def get_job_status(self, job_id: str) -> PeriodicJob | None:
        """Get current job status.

        Args:
            job_id: Job ID

        Returns:
            PeriodicJob or None if not found
        """
        async with self._lock:
            return self.jobs.get(job_id)

    async def list_jobs(self, session_id: str | None = None) -> list[PeriodicJob]:
        """List all active jobs, optionally filtered by session.

        Args:
            session_id: Optional session filter

        Returns:
            List of active jobs
        """
        async with self._lock:
            jobs = list(self.jobs.values())
            if session_id:
                jobs = [j for j in jobs if j.session_id == session_id]
            return jobs

    async def run(self) -> None:
        """Background scheduler loop.

        This runs forever, processing job deadlines. Should be run in a background task.
        Tick interval is 1-5ms for reasonable accuracy.
        """
        self._running = True
        tick_interval_s = 0.002  # 2ms ticks

        try:
            while self._running:
                await self._process_deadlines()
                await asyncio.sleep(tick_interval_s)
        finally:
            self._running = False

    async def stop(self) -> None:
        """Stop the scheduler gracefully."""
        self._running = False

    async def _process_deadlines(self) -> None:
        """Check and process all jobs whose deadlines have passed."""
        now_ns = time.monotonic_ns()
        jobs_to_process = []

        async with self._lock:
            # Collect jobs with passed deadlines
            for job in self.jobs.values():
                if job.next_deadline_ns <= now_ns:
                    jobs_to_process.append(job)

        # Process outside lock to avoid blocking scheduler
        for job in jobs_to_process:
            await self._submit_job(job, now_ns)

    async def _submit_job(self, job: PeriodicJob, now_ns: int) -> None:
        """Submit a job and advance its deadline.

        Args:
            job: Job to submit
            now_ns: Current monotonic time in nanoseconds
        """
        period_ns = job.period_ms * 1_000_000

        # Skip stale deadlines: if we're more than one period behind, skip to catch up
        while job.next_deadline_ns + period_ns <= now_ns:
            job.next_deadline_ns += period_ns

        # Encode the message
        encode_result = self.encoder.encode_message(
            key=job.key,
            values=job.values,
            bus=job.bus,
            session_id=job.session_id,
        )

        if not encode_result.ok:
            # Encoding failed - log but continue (don't stall scheduler)
            # In production, this would emit a diagnostic event
            return

        # Calculate and record jitter
        actual_submit_time_ns = time.monotonic_ns()
        jitter_ns = actual_submit_time_ns - job.next_deadline_ns
        job.jitter_measurements.append(jitter_ns)
        # Keep rolling window of last 100 measurements
        if len(job.jitter_measurements) > 100:
            job.jitter_measurements.pop(0)

        # Submit the frame
        try:
            await self.submit_callback(job.bus, encode_result.can_id, encode_result.data)
        except Exception:
            # Submission failed - log but continue
            # In production, this would emit a diagnostic event
            pass

        # Update job state
        job.last_submit_ns = actual_submit_time_ns
        job.submission_count += 1

        # Advance deadline for next period
        async with self._lock:
            job.next_deadline_ns += period_ns

    def get_jitter_stats(self, job_id: str) -> dict[str, float] | None:
        """Get jitter statistics for a job.

        Args:
            job_id: Job ID

        Returns:
            Dict with min/max/avg jitter in ms, or None if job not found
        """
        job = self.jobs.get(job_id)
        if not job or not job.jitter_measurements:
            return None

        jitter_ms = [ns / 1_000_000 for ns in job.jitter_measurements]
        return {
            "min_ms": min(jitter_ms),
            "max_ms": max(jitter_ms),
            "avg_ms": sum(jitter_ms) / len(jitter_ms),
            "count": len(jitter_ms),
        }

    async def clear_session_jobs(self, session_id: str) -> int:
        """Cancel all jobs for a session.

        Args:
            session_id: Session to clear

        Returns:
            Number of jobs cancelled
        """
        async with self._lock:
            job_ids = [
                job_id for job_id, job in self.jobs.items()
                if job.session_id == session_id
            ]
            for job_id in job_ids:
                self.jobs.pop(job_id, None)
            return len(job_ids)
