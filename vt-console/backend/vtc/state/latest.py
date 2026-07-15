"""Latest-value store keyed by (bus, can_id) (workplan §1.5).

Holds the latest decoded observation per runtime identity plus a monotonic
snapshot sequence for gap detection. Thread-safe: the router (writer thread) and
freshness ager mutate under the lock; snapshots return deep copies so concurrent
re-aging cannot race with response serialization.
"""

from __future__ import annotations

import threading

from vtc import protocol_bridge as proto
from vtc.pipeline.freshness import classify
from vtc.models.state import LatestStateSnapshot, MessageState


class LatestStore:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._messages: dict[tuple[str, int], MessageState] = {}
        self._sequence = 0

    def snapshot(self) -> LatestStateSnapshot:
        with self._lock:
            self._sequence += 1
            messages = [m.model_copy(deep=True) for m in self._messages.values()]
            return LatestStateSnapshot(
                sequence=self._sequence,
                wire_hash=proto.WIRE_HASH,
                messages=messages,
            )

    def upsert(self, state: MessageState) -> None:
        with self._lock:
            self._messages[(state.bus, state.can_id)] = state

    def reclassify_freshness(self, now_ns: int) -> None:
        """Re-age every message in place against its cycle (freshness ager)."""
        with self._lock:
            for st in self._messages.values():
                cycle_ms = (
                    int(round(1000 / st.expected_rate_hz))
                    if st.expected_rate_hz
                    else 0
                )
                st.freshness = classify(
                    st.validation_status, st.last_seen_ns, cycle_ms, now_ns
                )
