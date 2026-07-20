"""Latest-value store keyed by (bus, can_id) (workplan §1.5)."""

from __future__ import annotations

import threading
import time

from control_toolkit import protocol_bridge as proto
from control_toolkit.models.state import LatestStateSnapshot, MessageState
from control_toolkit.pipeline.freshness import classify


class LatestStore:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._messages: dict[tuple[str, int], MessageState] = {}
        # For observed rate: previous timestamps per key
        self._prev_seen_ns: dict[tuple[str, int], int] = {}
        self._sequence = 0

    def get_messages_map(self) -> dict[tuple[str, int], MessageState]:
        with self._lock:
            return {k: v.model_copy(deep=True) for k, v in self._messages.items()}

    def snapshot(self) -> LatestStateSnapshot:
        with self._lock:
            self._sequence += 1
            messages = [m.model_copy(deep=True) for m in self._messages.values()]
            now_ns = time.monotonic_ns()
            for message in messages:
                if message.last_seen_ns is not None:
                    message.age_ms = max(0.0, (now_ns - message.last_seen_ns) / 1_000_000)
            return LatestStateSnapshot(
                sequence=self._sequence,
                wire_hash=proto.WIRE_HASH,
                messages=messages,
            )

    def upsert(self, state: MessageState) -> MessageState:
        """Insert/update and compute observed rate from inter-arrival time."""
        key = (state.bus, state.can_id)
        with self._lock:
            prev_ns = self._prev_seen_ns.get(key)
            if (
                state.last_seen_ns is not None
                and prev_ns is not None
                and state.last_seen_ns > prev_ns
            ):
                dt_s = (state.last_seen_ns - prev_ns) / 1e9
                if dt_s > 0:
                    state.observed_rate_hz = 1.0 / dt_s
            if state.last_seen_ns is not None:
                self._prev_seen_ns[key] = state.last_seen_ns
            self._messages[key] = state
            return state

    def reclassify_freshness(self, now_ns: int) -> None:
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
