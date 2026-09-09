"""Bench wall-clock + timing helpers.

All bench timing is referenced to a single monotonic clock so scenario events,
capture samples (host-receive ``ts``) and physical-input markers are comparable.
"""
from __future__ import annotations

import time
from typing import Callable, Optional, TypeVar

T = TypeVar("T")


class Clock:
    """Monotonic second clock with wall-clock companions for trace headers."""

    def __init__(self) -> None:
        self._epoch = time.time()
        self._mono = time.perf_counter()

    def now(self) -> float:
        return time.perf_counter() - self._mono

    def wall(self) -> float:
        return time.time()

    def started_at(self) -> float:
        return self._epoch


def wait_until(predicate: Callable[[], bool], timeout: float, step: float = 0.01) -> float:
    """Poll ``predicate`` until true; return seconds elapsed. Raises on timeout."""
    deadline = time.perf_counter() + timeout
    while time.perf_counter() < deadline:
        if predicate():
            return time.perf_counter() + timeout - deadline
        time.sleep(step)
    raise TimeoutError(f"predicate not satisfied within {timeout:.2f}s")


def wait_result(fn: Callable[[], Optional[T]], timeout: float, step: float = 0.01) -> T:
    """Poll ``fn`` until it returns a non-None result; return it. Raises on timeout."""
    deadline = time.perf_counter() + timeout
    while time.perf_counter() < deadline:
        result = fn()
        if result is not None:
            return result
        time.sleep(step)
    raise TimeoutError(f"no result within {timeout:.2f}s")
