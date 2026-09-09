"""Base restbus node: a periodic transmitter owned by the PC.

Restbus nodes reproduce only the *missing* actors (Host, SES, SEB). No RT/SYS/
MTR controller logic is ported here — those are always real firmware on the
bench or (future) vECUs behind the same link seam.

Fault injection is expressed by changing what the node emits at the boundary:
  - ``fault_mode = "silent"``  stop transmitting
  - ``fault_mode = "frozen"``  keep sending but stop advancing counters
  - ``dropped_ids``            per-frame-id drop (e.g. drop only the heartbeat)
  - ``corrupt``                per-frame-id payload corruption (bad CRC etc.)
"""
from __future__ import annotations

import threading
import time
from abc import ABC, abstractmethod
from typing import Callable, Optional

from bench.can import CanLink
from bench.wire import encode_message

FAULT_HEALTHY = "healthy"
FAULT_SILENT = "silent"
FAULT_FROZEN = "frozen"


class RestbusNode(ABC):
    name = "node"
    bus = "low"
    period_s = 0.010

    def __init__(self, link: CanLink, bus: Optional[str] = None):
        self._link = link
        if bus is not None:
            self.bus = bus
        self.fault_mode: str = FAULT_HEALTHY
        self.dropped_ids: set[int] = set()
        self.corrupt: list[tuple[Callable[[int], bool], Callable[[bytes], bytes]]] = []
        self.sent_count: dict[int, int] = {}
        self._counters: dict[str, int] = {}
        self.state: dict = {}
        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._next = 0.0

    # ── lifecycle ───────────────────────────────────────────────────────
    def start(self) -> None:
        self._stop.clear()
        self._next = time.perf_counter()
        self._thread = threading.Thread(target=self._run, name=f"restbus-{self.name}", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)
            self._thread = None

    def _run(self) -> None:
        while not self._stop.is_set():
            now = time.perf_counter()
            if now >= self._next:
                self._next += self.period_s
                if self.fault_mode != FAULT_SILENT:
                    try:
                        self._emit()
                    except Exception:  # noqa: BLE001 - a broken peer must not kill the suite
                        pass
            sleep = max(0.0, self._next - time.perf_counter())
            if sleep > 0.0005:
                time.sleep(sleep)

    @abstractmethod
    def _emit(self) -> None: ...

    # ── counter helper (frozen => do not advance) ───────────────────────
    def advance(self, name: str, modulus: int = 256) -> int:
        value = self._counters.get(name, 0)
        if self.fault_mode != FAULT_FROZEN:
            self._counters[name] = (value + 1) % modulus
        return value % modulus

    # ── transmit through fault pipeline ─────────────────────────────────
    def send(self, can_id: int, values: dict) -> None:
        if can_id in self.dropped_ids:
            return
        try:
            data = encode_message(self.bus, can_id, values)
        except (KeyError, ValueError):
            return
        for wants, mutate in self.corrupt:
            if wants(can_id):
                data = mutate(data)
        self._link.send(self.bus, can_id, data)
        self.sent_count[can_id] = self.sent_count.get(can_id, 0) + 1

    def drop(self, can_id: int, dropped: bool = True) -> None:
        if dropped:
            self.dropped_ids.add(can_id)
        else:
            self.dropped_ids.discard(can_id)
