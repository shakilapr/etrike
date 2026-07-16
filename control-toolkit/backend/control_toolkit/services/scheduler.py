"""Backend-owned periodic TX scheduler with per-period re-encode."""

from __future__ import annotations

import asyncio
import time
import uuid
from dataclasses import dataclass, field
from typing import Any, Callable

from control_toolkit.models.frames import FrameSource
from control_toolkit.services.tx_gate import TxGate, TxResult


@dataclass
class PeriodicJob:
    job_id: str
    bus: str
    key: str | None
    can_id: int | None
    values: dict[str, Any]
    period_s: float
    owner: str
    source: FrameSource
    next_deadline: float
    counter_field: str | None = None
    counter_value: int = 0
    missed: int = 0
    last_result: str | None = None
    cancel: bool = False


class Scheduler:
    def __init__(self, tx_gate: TxGate) -> None:
        self._tx = tx_gate
        self._jobs: dict[str, PeriodicJob] = {}
        self._task: asyncio.Task | None = None
        self._running = False

    def start(self) -> None:
        if self._task is None:
            self._running = True
            self._task = asyncio.create_task(self._loop())

    def stop(self) -> None:
        self._running = False
        self.cancel_all()
        if self._task is not None:
            self._task.cancel()
            self._task = None

    def job_ids(self) -> list[str]:
        return list(self._jobs.keys())

    def schedule(
        self,
        *,
        bus: str,
        values: dict[str, Any],
        period_ms: float,
        key: str | None = None,
        can_id: int | None = None,
        owner: str = "scheduler",
        source: FrameSource = FrameSource.INJECTION,
        counter_field: str | None = None,
    ) -> str:
        if period_ms <= 0:
            raise ValueError("period_ms must be positive")
        job_id = f"job_{uuid.uuid4().hex[:10]}"
        now = time.monotonic()
        self._jobs[job_id] = PeriodicJob(
            job_id=job_id,
            bus=bus,
            key=key,
            can_id=can_id,
            values=dict(values),
            period_s=period_ms / 1000.0,
            owner=owner,
            source=source,
            next_deadline=now,
            counter_field=counter_field,
        )
        return job_id

    def cancel(self, job_id: str) -> bool:
        job = self._jobs.pop(job_id, None)
        if job is None:
            return False
        job.cancel = True
        return True

    def update_values(self, job_id: str, values: dict[str, Any]) -> bool:
        """Hot-update engineering values for the next period (re-encode uses new dict)."""
        job = self._jobs.get(job_id)
        if job is None:
            return False
        job.values = dict(values)
        return True

    def cancel_all(self) -> int:
        n = len(self._jobs)
        self._jobs.clear()
        return n

    async def _loop(self) -> None:
        try:
            while self._running:
                now = time.monotonic()
                due = [j for j in list(self._jobs.values()) if j.next_deadline <= now]
                for job in due:
                    # Skip stale missed periods without burst catch-up.
                    while job.next_deadline + job.period_s <= now:
                        job.next_deadline += job.period_s
                        job.missed += 1
                    values = dict(job.values)
                    if job.counter_field:
                        values[job.counter_field] = job.counter_value & 0xFF
                        job.counter_value = (job.counter_value + 1) & 0xFF
                    result: TxResult = self._tx.submit(
                        bus=job.bus,
                        key=job.key,
                        can_id=job.can_id,
                        values=values,
                        owner=job.owner,
                        source=job.source,
                        claim_ownership=True,
                        lease_ttl_s=max(2.0, job.period_s * 3),
                    )
                    job.last_result = result.disposition
                    if result.disposition == "rejected" and result.reason and result.reason.startswith(
                        "ownership:"
                    ):
                        # Conflict: stop this job.
                        self._jobs.pop(job.job_id, None)
                        continue
                    job.next_deadline += job.period_s
                await asyncio.sleep(0.002)
        except asyncio.CancelledError:
            return
