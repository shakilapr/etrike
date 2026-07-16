"""Per-message freshness tracker (workplan §1.5).

Freshness ages on its own clock: a periodic message becomes Late then Missing
purely from elapsed time, even when no new frames arrive. Thresholds derive from
each message's YAML cycle — never a global constant (can-analyzer-research §7.4).

State model (workplan §1.5):
  Unseen -> Live -> Late -> Missing, plus Invalid (codec failure). Frozen
  (counter stall) and Recovering (post-Missing hysteresis) are deferred; they
  need counter-advance tracking added in §5/§6.

Thresholds (architecture §18.1 "max visual age while Live = max(150ms, 2× period)"):
  Late    when age > max(150 ms, 2 × cycle)
  Missing when age > max(500 ms, 5 × cycle)

An event message (cycle_ms == 0) has no periodic expectation, so it never ages to
Late/Missing — once seen it reflects only its validity.
"""

from __future__ import annotations

import asyncio
import time
from typing import Callable

from control_toolkit.models.state import FreshnessState

LATE_FLOOR_MS = 150
MISSING_FLOOR_MS = 500


def late_threshold_ms(cycle_ms: int) -> float:
    return max(LATE_FLOOR_MS, 2 * cycle_ms)


def missing_threshold_ms(cycle_ms: int) -> float:
    return max(MISSING_FLOOR_MS, 5 * cycle_ms)


def classify(
    validation_status: str | None,
    last_seen_ns: int | None,
    cycle_ms: int,
    now_ns: int,
) -> FreshnessState:
    """Pure freshness classification. Idempotent; safe to call repeatedly."""
    if last_seen_ns is None:
        return FreshnessState.UNSEEN

    fresh_state = (
        FreshnessState.LIVE
        if validation_status in (None, "ok")
        else FreshnessState.INVALID
    )

    if cycle_ms <= 0:
        # Event/aperiodic (or unknown) message: no staleness expectation.
        return fresh_state

    age_ms = (now_ns - last_seen_ns) / 1_000_000
    if age_ms > missing_threshold_ms(cycle_ms):
        return FreshnessState.MISSING
    if age_ms > late_threshold_ms(cycle_ms):
        return FreshnessState.LATE
    return fresh_state


class FreshnessAger:
    """Background task that re-ages the latest-value store on a fixed interval."""

    def __init__(
        self,
        latest,
        *,
        interval_s: float = 0.05,
        now_ns: Callable[[], int] = time.monotonic_ns,
    ) -> None:
        self._latest = latest
        self._interval = interval_s
        self._now = now_ns
        self._running = False

    def age_now(self) -> None:
        self._latest.reclassify_freshness(self._now())

    async def run(self) -> None:
        self._running = True
        while self._running:
            self.age_now()
            await asyncio.sleep(self._interval)

    def stop(self) -> None:
        self._running = False
