"""Latest-value store keyed by (bus, can_id) (workplan §1.5)."""

from collections import deque
import statistics
import threading
import time

from control_toolkit import protocol_bridge as proto
from control_toolkit.models.state import LatestStateSnapshot, MessageState
from control_toolkit.pipeline.freshness import classify


class LatestStore:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._messages: dict[tuple[str, int], MessageState] = {}
        # For observed rate: previous timestamps per key and recent valid gaps
        self._prev_seen_ns: dict[tuple[str, int], int] = {}
        self._gaps_s: dict[tuple[str, int], deque[float]] = {}
        self._last_stable_rate: dict[tuple[str, int], float] = {}
        self._sequence = 0

    def get_messages_map(self) -> dict[tuple[str, int], MessageState]:
        with self._lock:
            return {k: v.model_copy(deep=True) for k, v in self._messages.items()}

    def snapshot(self, now_ns: int | None = None) -> LatestStateSnapshot:
        with self._lock:
            self._sequence += 1
            messages = [m.model_copy(deep=True) for m in self._messages.values()]
            if now_ns is None:
                now_ns = time.monotonic_ns()
            for message in messages:
                if message.last_seen_ns is not None:
                    message.age_ms = max(0.0, (now_ns - message.last_seen_ns) / 1_000_000)
                    cycle_ms = (
                        int(round(1000 / message.expected_rate_hz))
                        if message.expected_rate_hz
                        else 0
                    )
                    message.freshness = classify(
                        message.validation_status, message.last_seen_ns, cycle_ms, now_ns
                    )
            return LatestStateSnapshot(
                sequence=self._sequence,
                wire_hash=proto.WIRE_HASH,
                messages=messages,
            )

    def upsert(self, state: MessageState) -> MessageState:
        """Insert/update and compute robust observed rate from median inter-arrival time."""
        key = (state.bus, state.can_id)
        with self._lock:
            prev_ns = self._prev_seen_ns.get(key)
            if state.last_seen_ns is not None:
                if prev_ns is not None and state.last_seen_ns > prev_ns:
                    dt_s = (state.last_seen_ns - prev_ns) / 1e9
                    min_dt = 0.001
                    if state.expected_rate_hz and state.expected_rate_hz > 0:
                        min_dt = max(min_dt, (1.0 / state.expected_rate_hz) * 0.25)

                    if dt_s >= min_dt:
                        if key not in self._gaps_s:
                            self._gaps_s[key] = deque(maxlen=8)
                        self._gaps_s[key].append(dt_s)

                self._prev_seen_ns[key] = state.last_seen_ns

            gaps = self._gaps_s.get(key)
            if gaps and len(gaps) >= 1:
                median_dt = statistics.median(gaps)
                if median_dt > 0:
                    rate = 1.0 / median_dt
                    if state.expected_rate_hz and state.expected_rate_hz > 0 and rate > 5.0 * state.expected_rate_hz:
                        rate = self._last_stable_rate.get(key, rate)
                    else:
                        self._last_stable_rate[key] = rate
                    state.observed_rate_hz = rate
            elif key in self._last_stable_rate:
                state.observed_rate_hz = self._last_stable_rate[key]

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

    def clear(self) -> None:
        """Drop all latest-value rows (transport/profile switch — no ghost traffic)."""
        with self._lock:
            self._messages.clear()
            self._prev_seen_ns.clear()
            self._sequence += 1
