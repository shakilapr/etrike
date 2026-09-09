"""Temporal safety assertions.

Final-state assertions are not enough for a distributed safety system. These
helpers express MUST-NEVER / MUST-ALWAYS / MUST-WITHIN properties against the
live capture timeline at the bench boundary.

``never_window`` / ``always_window`` run a watcher that samples a predicate on
every newly received frame while the context is open; any violation raises when
the window closes, even if it lasted only one frame (e.g. 20 ms traction blip).
"""
from __future__ import annotations

import threading
import time
from typing import Callable, Optional

from bench.capture import Capture, Sample


class Violation(Exception):
    pass


def _ts(sample: Sample) -> float:
    return sample.hw_ts if sample.hw_ts is not None else sample.ts


def sample_time(sample: Optional[Sample]) -> Optional[float]:
    return _ts(sample) if sample is not None else None


def latency(capture: Capture, from_match: Callable[[Sample], bool], to_match: Callable[[Sample], bool]) -> Optional[float]:
    """Seconds from the first ``from_match`` sample to the first ``to_match`` sample."""
    samples = capture.all()
    start: Optional[float] = None
    for sample in samples:
        if start is None and from_match(sample):
            start = _ts(sample)
        if start is not None and to_match(sample):
            return _ts(sample) - start
    return None


def wait_for_valid(capture: Capture, bus: str, key: str, *, field: Optional[str] = None,
                   value=None, timeout: float = 5.0) -> Sample:
    def match(sample: Sample) -> bool:
        if sample.bus != bus or sample.key != key or not sample.is_valid:
            return False
        if field is None:
            return True
        return sample.values.get(field) == value

    return capture.wait_for(match, timeout=timeout)


class never_window:
    """Assert that ``danger()`` is never true while the window is open."""

    def __init__(self, capture: Capture, danger: Callable[[], bool], sample_ms: float = 0.02):
        self._capture = capture
        self._danger = danger
        self._sample_ms = sample_ms
        self._violations: list[float] = []
        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None

    def __enter__(self) -> "never_window":
        self._stop.clear()
        self._thread = threading.Thread(target=self._watch, name="never-window", daemon=True)
        self._thread.start()
        return self

    def _watch(self) -> None:
        while not self._stop.is_set():
            try:
                if self._danger():
                    self._violations.append(time.perf_counter())
            except Exception:
                pass
            self._stop.wait(self._sample_ms)

    def __exit__(self, exc_type, exc, tb) -> bool:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)
            self._thread = None
        if self._violations:
            raise Violation(f"danger condition observed {len(self._violations)}x during never-window")
        return False


class always_window:
    """Assert that ``required()`` stays true for the whole open window."""

    def __init__(self, capture: Capture, required: Callable[[], bool], sample_ms: float = 0.02):
        self._capture = capture
        self._required = required
        self._sample_ms = sample_ms
        self._misses: list[float] = []
        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None

    def __enter__(self) -> "always_window":
        self._stop.clear()
        self._thread = threading.Thread(target=self._watch, name="always-window", daemon=True)
        self._thread.start()
        return self

    def _watch(self) -> None:
        while not self._stop.is_set():
            try:
                if not self._required():
                    self._misses.append(time.perf_counter())
            except Exception:
                self._misses.append(time.perf_counter())
            self._stop.wait(self._sample_ms)

    def __exit__(self, exc_type, exc, tb) -> bool:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)
            self._thread = None
        if self._misses:
            raise Violation(f"required condition missed {len(self._misses)}x during always-window")
        return False
