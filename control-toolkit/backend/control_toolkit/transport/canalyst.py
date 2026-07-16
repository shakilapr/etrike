"""CANalyst-II physical transport (architecture §4.4).

Channel map (architecture, corrected vs debug-tool reverse):
  Channel 0 → High @ 500 kbit/s
  Channel 1 → Low  @ 500 kbit/s

Uses python-can ``canalystii``. RX is drained on a worker thread into a bounded
queue with overflow counters (never silent loss).
"""

from __future__ import annotations

import queue
import threading
import time
from typing import Any

import can

from control_toolkit.models.adapter import (
    AdapterHealth,
    AdapterStatus,
    Capability,
    ChannelActivity,
    ChannelState,
)
from control_toolkit.models.frames import ChannelId, Direction, FrameSource, RawFrameEnvelope

# Architecture: CH0 High, CH1 Low
_HW_TO_BUS: dict[int, ChannelId] = {0: ChannelId.HIGH, 1: ChannelId.LOW}
_BUS_TO_HW: dict[ChannelId, int] = {v: k for k, v in _HW_TO_BUS.items()}


_probe_cache: tuple[float, bool, str] | None = None
_PROBE_TTL_S = 5.0


def canalyst_available(
    device_index: int = 0, bitrate: int = 500_000, *, force: bool = False
) -> tuple[bool, str]:
    """Probe whether a CANalyst-II can be opened briefly (cached ~5s)."""
    global _probe_cache
    import time

    now = time.monotonic()
    if not force and _probe_cache is not None:
        ts, ok, reason = _probe_cache
        if now - ts < _PROBE_TTL_S:
            return ok, reason
    try:
        bus = can.Bus(
            interface="canalystii",
            channel=0,
            bitrate=bitrate,
            device=device_index,
        )
        try:
            bus.shutdown()
        except Exception:
            pass
        _probe_cache = (now, True, "CANalyst-II detected")
        return True, "CANalyst-II detected"
    except Exception as exc:  # noqa: BLE001
        reason = str(exc) or "CANalyst-II not available"
        _probe_cache = (now, False, reason)
        return False, reason


class CanalystTransportAdapter:
    """Dual-channel CANalyst-II adapter implementing the Transport protocol."""

    def __init__(
        self,
        *,
        rx_queue_maxsize: int = 65536,
        bitrate: int = 500_000,
        device_index: int = 0,
        poll_ms: float = 2.0,
    ) -> None:
        self._epoch = 0
        self._health = AdapterHealth.ABSENT
        self._bitrate = bitrate
        self._device_index = device_index
        self._poll_s = max(poll_ms, 1.0) / 1000.0
        self._lock = threading.Lock()
        self._queue: queue.Queue[RawFrameEnvelope] = queue.Queue(maxsize=rx_queue_maxsize)
        self._bus: can.BusABC | None = None
        self._thread: threading.Thread | None = None
        self._stop = threading.Event()
        self._state: dict[ChannelId, ChannelState] = {
            ch: ChannelState(channel=ch.value) for ch in _BUS_TO_HW
        }
        self._seq: dict[ChannelId, int] = {ch: 0 for ch in _BUS_TO_HW}
        self._overflow = 0
        self._queue_high_water = 0
        self._open_error: str | None = None

    @property
    def epoch(self) -> int:
        return self._epoch

    @property
    def capability(self) -> Capability:
        return Capability(
            hw_timestamps=True,
            tx_echo=False,
            listen_only=None,
            bus_off_reporting=False,
            tec_rec_reporting=False,
        )

    def status(self) -> AdapterStatus:
        with self._lock:
            channels = {ch.value: st.model_copy() for ch, st in self._state.items()}
            identity = "canalystii"
            if self._open_error:
                identity = f"canalystii ({self._open_error[:60]})"
            health = self._health
            epoch = self._epoch
        return AdapterStatus(
            identity=identity,
            health=health,
            adapter_epoch=epoch,
            capability=self.capability,
            channels=channels,
        )

    def open(self) -> None:
        if self._health in (AdapterHealth.OPEN, AdapterHealth.ACTIVE):
            return
        self._stop.clear()
        self._health = AdapterHealth.OPENING
        self._epoch += 1
        try:
            # Single multi-channel bus — architecture CH0=High, CH1=Low.
            self._bus = can.Bus(
                interface="canalystii",
                channel=(0, 1),
                bitrate=self._bitrate,
                device=self._device_index,
                rx_queue_size=None,  # unbounded short buffer; we drain immediately
            )
            self._thread = threading.Thread(
                target=self._rx_loop,
                name="canalyst-rx",
                daemon=True,
            )
            self._thread.start()
            self._health = AdapterHealth.OPEN
            self._open_error = None
        except Exception as exc:  # noqa: BLE001
            self._open_error = str(exc)
            self.close()
            self._health = AdapterHealth.ABSENT
            raise

    def _rx_loop(self) -> None:
        assert self._bus is not None
        bus = self._bus
        while not self._stop.is_set():
            try:
                msg = bus.recv(timeout=self._poll_s)
            except Exception:
                if self._stop.is_set():
                    break
                with self._lock:
                    self._health = AdapterHealth.DEGRADED
                time.sleep(self._poll_s)
                continue
            if msg is None:
                continue
            hw_ch = getattr(msg, "channel", None)
            if hw_ch is None:
                continue
            try:
                hw_ch_i = int(hw_ch)
            except (TypeError, ValueError):
                continue
            channel = _HW_TO_BUS.get(hw_ch_i)
            if channel is None:
                continue
            arrival_ns = time.monotonic_ns()
            with self._lock:
                self._seq[channel] += 1
                seq = self._seq[channel]
            dlc = msg.dlc if msg.dlc is not None else len(msg.data)
            device_ts = None
            if getattr(msg, "timestamp", None) is not None:
                try:
                    device_ts = int(float(msg.timestamp) * 10_000)
                except (TypeError, ValueError):
                    device_ts = None
            env = RawFrameEnvelope(
                adapter_epoch=self._epoch,
                channel=channel,
                device_timestamp=device_ts,
                backend_arrival_ns=arrival_ns,
                can_id=int(msg.arbitration_id),
                is_extended=bool(msg.is_extended_id),
                is_remote=bool(msg.is_remote_frame),
                dlc=dlc,
                data=bytes(msg.data[:dlc]),
                channel_sequence=seq,
                direction=Direction.RX,
                source=FrameSource.PHYSICAL,
            )
            self._enqueue(channel, env)

    def _enqueue(self, channel: ChannelId, env: RawFrameEnvelope) -> None:
        try:
            self._queue.put_nowait(env)
        except queue.Full:
            with self._lock:
                self._overflow += 1
                self._state[channel].rx_overflow = self._overflow
            return
        with self._lock:
            st = self._state[channel]
            st.rx_count += 1
            st.last_rx_ns = env.backend_arrival_ns
            st.activity = ChannelActivity.ACTIVE
            qsize = self._queue.qsize()
            if qsize > self._queue_high_water:
                self._queue_high_water = qsize
            st.queue_high_water = self._queue_high_water
            if self._health in (AdapterHealth.OPEN, AdapterHealth.QUIET):
                self._health = AdapterHealth.ACTIVE

    def poll(self, max_items: int = 256, timeout: float = 0.0) -> list[RawFrameEnvelope]:
        out: list[RawFrameEnvelope] = []
        try:
            if timeout > 0:
                out.append(self._queue.get(timeout=timeout))
            else:
                out.append(self._queue.get_nowait())
        except queue.Empty:
            return out
        while len(out) < max_items:
            try:
                out.append(self._queue.get_nowait())
            except queue.Empty:
                break
        return out

    def send(self, frame: RawFrameEnvelope) -> str:
        if self._bus is None:
            return "rejected"
        hw = _BUS_TO_HW.get(frame.channel)
        if hw is None:
            return "rejected"
        try:
            self._bus.send(
                can.Message(
                    arbitration_id=frame.can_id,
                    is_extended_id=frame.is_extended,
                    is_remote_frame=frame.is_remote,
                    dlc=frame.dlc,
                    data=bytes(frame.data[: frame.dlc]),
                    channel=hw,
                )
            )
            with self._lock:
                self._state[frame.channel].tx_count += 1
            return "submitted"
        except Exception:
            return "rejected"

    def inject(self, *args: Any, **kwargs: Any) -> None:
        raise RuntimeError("inject not supported on physical CANalyst transport")

    def close(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=1.5)
            self._thread = None
        if self._bus is not None:
            try:
                self._bus.shutdown()
            except Exception:
                pass
            self._bus = None
        self._health = AdapterHealth.CLOSED
