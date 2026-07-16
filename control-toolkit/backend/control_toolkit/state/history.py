"""Bounded frame history for chronological monitor (architecture §5).

Fixed-size ring of immutable RawFrameEnvelope observations. Overflow is counted
and visible — never a silent deque(maxlen) eviction without metrics.
"""

from __future__ import annotations

import threading
from collections import deque

from control_toolkit.models.frames import RawFrameEnvelope


class FrameHistory:
    def __init__(self, capacity: int = 4096) -> None:
        if capacity < 1:
            raise ValueError("capacity must be >= 1")
        self._capacity = capacity
        self._lock = threading.Lock()
        self._ring: deque[RawFrameEnvelope] = deque(maxlen=capacity)
        self._dropped = 0
        self._total = 0

    @property
    def capacity(self) -> int:
        return self._capacity

    def append(self, env: RawFrameEnvelope) -> None:
        with self._lock:
            if len(self._ring) == self._capacity:
                self._dropped += 1
            self._ring.append(env)
            self._total += 1

    def snapshot(self, limit: int | None = None) -> list[RawFrameEnvelope]:
        with self._lock:
            items = list(self._ring)
        if limit is not None and limit >= 0:
            return items[-limit:]
        return items

    def metrics(self) -> dict[str, int]:
        with self._lock:
            return {
                "capacity": self._capacity,
                "size": len(self._ring),
                "total_appended": self._total,
                "dropped": self._dropped,
            }

    def clear(self) -> None:
        with self._lock:
            self._ring.clear()
