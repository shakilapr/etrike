"""Frame capture: drains the link, decodes every frame and keeps a timeline.

All message keys/ids/dlc come from the generated protocol catalog (``bench.wire``);
a contract change fails loudly at import instead of corrupting a bench run.
"""
from __future__ import annotations

import threading
import time
from typing import Callable, Optional

from .can import CanLink
from .wire import Sample, decode_message


class Capture:
    """Observer that drains the link, decodes every frame and keeps a timeline.

    The capture worker owns the receive loop. Restbus nodes and scenarios push
    state through this single timeline so temporal assertions see one ordered,
    hardware-timestamped stream.
    """

    def __init__(self, link: CanLink, traces_path=None):
        self._link = link
        self._samples: list[Sample] = []
        self._latest: dict[tuple[str, int], Sample] = {}
        self._lock = threading.Condition()
        self._thread: Optional[threading.Thread] = None
        self._stop = threading.Event()
        self.traces_path = traces_path
        self.started_at = 0.0

    @property
    def link(self) -> CanLink:
        return self._link

    def start(self) -> None:
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, name="system-test-capture", daemon=True)
        self.started_at = time.perf_counter()
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)
            self._thread = None

    def _run(self) -> None:
        while not self._stop.is_set():
            rec = self._link.recv(timeout=0.05)
            if rec is None:
                continue
            key, values = decode_message(rec.bus, rec.arbitration_id, rec.data)
            sample = Sample(
                bus=rec.bus,
                can_id=rec.arbitration_id,
                key=key,
                values=values,
                data=rec.data,
                ts=rec.ts,
                hw_ts=rec.hw_ts,
            )
            with self._lock:
                self._samples.append(sample)
                self._latest[(rec.bus, rec.arbitration_id)] = sample
                self._lock.notify_all()

    # ── queries ─────────────────────────────────────────────────────────
    def all(self) -> list[Sample]:
        with self._lock:
            return list(self._samples)

    def latest(self, key: str) -> Optional[Sample]:
        with self._lock:
            for sample in reversed(self._samples):
                if sample.key == key:
                    return sample
        return None

    def latest_on(self, bus: str, key: str) -> Optional[Sample]:
        with self._lock:
            for sample in reversed(self._samples):
                if sample.bus == bus and sample.key == key:
                    return sample
        return None

    def wait_for(self, match: Callable[[Sample], bool], timeout: float) -> Sample:
        """Block until a decoded sample satisfies ``match`` or timeout elapses."""
        deadline = time.perf_counter() + timeout
        with self._lock:
            while True:
                for sample in self._samples:
                    if match(sample):
                        return sample
                remaining = deadline - time.perf_counter()
                if remaining <= 0:
                    raise TimeoutError(f"no matching frame within {timeout:.2f}s")
                self._lock.wait(timeout=remaining)

    def count(self, key: str) -> int:
        with self._lock:
            return sum(1 for s in self._samples if s.key == key)

    def clear(self) -> None:
        with self._lock:
            self._samples.clear()
            self._latest.clear()
