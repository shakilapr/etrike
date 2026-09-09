"""CAN link seam for the L9 system bench.

Two implementations share one interface so that every scenario is written once
against *external interfaces* and executed unchanged against either:

* ``CanalystLink``   - real dual-bus CANalyst-II (CH0=High, CH1=Low @ 500 kbit/s)
* ``VirtualLink``    - python-can ``virtual`` loopback used for host self-tests
                        of the harness itself (no ECU present).

Rules honoured by every scenario:
  - a scenario never reaches inside an ECU; it observes frames on the link and
    transmits the frames that only the bench/restbus is allowed to own.
  - timing analysis uses hardware timestamps (``hw_ts``) where the adapter
    provides them and monotonic ``ts`` otherwise.
"""
from __future__ import annotations

import threading
import time
from abc import ABC, abstractmethod
from dataclasses import dataclass
from typing import Optional

import can

from .config import BenchConfig

try:  # canalystii is an optional python-can extra; do not hard-depend in self-tests
    import canalystii  # noqa: F401
    _HAS_DRIVER = True
except Exception:  # pragma: no cover - exercised only without the driver installed
    _HAS_DRIVER = False


@dataclass
class RxRecord:
    """One received frame normalised across backends."""

    bus: str              # logical bus label: "high" | "low"
    arbitration_id: int
    dlc: int
    data: bytes
    ts: float             # monotonic seconds, host receive time (ordering)
    hw_ts: Optional[float] = None  # adapter hardware timestamp (CANalyst: 100us ticks)


def bus_present() -> bool:
    return _HAS_DRIVER


class CanLink(ABC):
    """A two-channel CAN link the bench may both transmit on and observe."""

    name = "abstract"

    @abstractmethod
    def send(self, bus: str, arbitration_id: int, data: bytes) -> None: ...

    @abstractmethod
    def recv(self, timeout: float = 0.05) -> Optional[RxRecord]: ...

    @abstractmethod
    def close(self) -> None: ...


class CanalystLink(CanLink):
    """Real CANalyst-II dual-channel link.

    A single CANalystIIBus with ``channel=(0, 1)``; RX messages carry a physical
    ``channel`` (0/1) that is mapped to the logical bus. TX routes by setting
    ``msg.channel``. CANalyst-II timestamps tick at 100 us and are reported as
    ``hw_ts``.
    """

    name = "canalystii"

    def __init__(self, cfg: BenchConfig) -> None:
        self._cfg = cfg
        self._tx_lock = threading.Lock()
        self._bus = can.Bus(
            interface="canalystii",
            channel=(cfg.channel_high, cfg.channel_low),
            bitrate=cfg.bitrate,
            device=cfg.device,
            rx_queue_size=None,
        )
        try:
            self._bus.RX_POLL_DELAY = cfg.poll_ms / 1000.0
        except Exception:
            pass

    def send(self, bus: str, arbitration_id: int, data: bytes) -> None:
        msg = can.Message(
            arbitration_id=arbitration_id,
            data=data,
            is_extended_id=False,
            channel=self._cfg.physical_channel_for(bus),
        )
        with self._tx_lock:
            self._bus.send(msg)

    def recv(self, timeout: float = 0.05) -> Optional[RxRecord]:
        msg = self._bus.recv(timeout=timeout)
        if msg is None:
            return None
        return RxRecord(
            bus=self._cfg.bus_for_physical_channel(msg.channel),
            arbitration_id=msg.arbitration_id,
            dlc=msg.dlc,
            data=bytes(msg.data),
            ts=time.perf_counter(),
            hw_ts=float(msg.timestamp) if msg.timestamp else None,
        )

    def close(self) -> None:
        try:
            self._bus.shutdown()
        except Exception:
            pass

    @staticmethod
    def probe(cfg: BenchConfig) -> tuple[bool, str]:
        if not bus_present():
            return False, "python-can canalystii driver not installed"
        bus = None
        try:
            bus = can.Bus(
                interface="canalystii",
                channel=(cfg.channel_high, cfg.channel_low),
                bitrate=cfg.bitrate,
                device=cfg.device,
            )
            bus.shutdown()
            return True, f"CANalyst-II device {cfg.device} present"
        except Exception as exc:  # noqa: BLE001
            return False, f"CANalyst-II probe failed: {exc}"
        finally:
            if bus is not None:
                try:
                    bus.shutdown()
                except Exception:
                    pass


class VirtualLink(CanLink):
    """python-can ``virtual`` loopback link for host self-tests.

    For each logical bus two virtual buses share one channel name: a TX bus the
    restbus/nodes send on and an observer bus the capture worker reads from
    (python-can's virtual interface delivers to every other bus with the same
    channel name, never back to the sender).
    """

    name = "loopback"

    def __init__(self, cfg: BenchConfig) -> None:
        self._cfg = cfg
        self._tx: dict[str, can.Bus] = {}
        self._rx: dict[str, can.Bus] = {}
        for bus in ("high", "low"):
            channel = cfg.loopback_channel(bus)
            self._tx[bus] = can.Bus(interface="virtual", channel=channel)
            self._rx[bus] = can.Bus(interface="virtual", channel=channel)

    def send(self, bus: str, arbitration_id: int, data: bytes) -> None:
        msg = can.Message(arbitration_id=arbitration_id, data=data, is_extended_id=False)
        self._tx[bus].send(msg)

    def recv(self, timeout: float = 0.05) -> Optional[RxRecord]:
        deadline = time.perf_counter() + timeout
        for bus in ("high", "low"):
            remaining = max(0.0, deadline - time.perf_counter())
            msg = self._rx[bus].recv(timeout=remaining)
            if msg is not None:
                return RxRecord(
                    bus=bus,
                    arbitration_id=msg.arbitration_id,
                    dlc=msg.dlc,
                    data=bytes(msg.data),
                    ts=time.perf_counter(),
                    hw_ts=None,
                )
        return None

    def close(self) -> None:
        for bus in self._tx.values():
            try:
                bus.shutdown()
            except Exception:
                pass
        for bus in self._rx.values():
            try:
                bus.shutdown()
            except Exception:
                pass


def open_link(cfg: BenchConfig) -> tuple[CanLink, str]:
    """Open the best available link and return ``(link, description)``.

    Raises ``RuntimeError`` when a physical link is required but unavailable.
    """
    transport = cfg.transport
    if transport == "loopback":
        return VirtualLink(cfg), "loopback virtual CAN"
    if transport == "canalystii":
        ok, reason = CanalystLink.probe(cfg)
        if not ok:
            raise RuntimeError(f"canalystii transport required but unavailable: {reason}")
        return CanalystLink(cfg), reason
    # auto: prefer physical, fall back to loopback self-test link.
    ok, reason = CanalystLink.probe(cfg)
    if ok:
        return CanalystLink(cfg), reason
    return VirtualLink(cfg), f"no physical bench ({reason}); using loopback self-test link"
