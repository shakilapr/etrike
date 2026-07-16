"""Internal event distribution (workplan §1.1, §1.6).

Fan-out of critical events (transport/diagnostic/error) to WebSocket subscribers.
Each subscriber owns a bounded, thread-safe queue: a slow client cannot stall
producers, and drops are counted, never silent (can-analyzer-research §6).

Thread-safe by design — the router runs in a worker thread and later phases
publish from there. Subscribers are drained by the stream sender task each batch
tick, so event latency is bounded by the batch interval.
"""

from __future__ import annotations

import queue
import threading


class Subscriber:
    def __init__(self, maxsize: int = 1000) -> None:
        self._queue: "queue.Queue[dict]" = queue.Queue(maxsize=maxsize)
        self.dropped = 0

    def offer(self, event: dict) -> bool:
        try:
            self._queue.put_nowait(event)
            return True
        except queue.Full:
            self.dropped += 1  # visible loss, never silent
            return False

    def drain(self, max_items: int = 256) -> list[dict]:
        out: list[dict] = []
        for _ in range(max_items):
            try:
                out.append(self._queue.get_nowait())
            except queue.Empty:
                break
        return out


class EventBus:
    def __init__(self) -> None:
        self._subs: set[Subscriber] = set()
        self._lock = threading.Lock()

    def subscribe(self, maxsize: int = 1000) -> Subscriber:
        sub = Subscriber(maxsize)
        with self._lock:
            self._subs.add(sub)
        return sub

    def unsubscribe(self, sub: Subscriber) -> None:
        with self._lock:
            self._subs.discard(sub)

    def publish(self, event: dict) -> None:
        with self._lock:
            subs = list(self._subs)
        for sub in subs:
            sub.offer(event)

    @property
    def subscriber_count(self) -> int:
        with self._lock:
            return len(self._subs)
