"""Virtual CAN transport adapter (workplan §1.3).

Uses python-can's ``virtual`` interface to create named High and Low buses with
no hardware. Blocking receive runs in a dedicated thread via ``can.Notifier``;
the listener does a constant-time copy of each frame into a bounded queue (no
decode). Queue overflow is counted with lost-frame evidence — never a silent
``deque(maxlen)`` eviction (can-analyzer-research §3). This is the transport used
by the Pure Software profile and all virtual integration tests.

python-can's virtual buses are shared by channel name within the process, so
each adapter instance uses a unique channel suffix. A separate emitter bus per
channel lets tests, injection, and synthetic peers publish frames that the
adapter observes as RX (the adapter's own bus uses ``receive_own_messages=False``,
so the virtual adapter has no TX echo — matching its capability record).
"""

from __future__ import annotations

import queue
import threading
import time
import uuid

import can

from control_toolkit.models.adapter import (
    AdapterHealth,
    AdapterStatus,
    Capability,
    ChannelActivity,
    ChannelState,
)
from control_toolkit.models.frames import ChannelId, Direction, FrameSource, RawFrameEnvelope

_CHANNELS: tuple[ChannelId, ...] = (ChannelId.HIGH, ChannelId.LOW)


class _QueueListener(can.Listener):
    """python-can listener: constant-time raw copy into a bounded queue.

    No decoding happens here — that is the router's job (§1.4). On queue overflow
    the frame is dropped and counted; the loss is visible, never silent.
    """

    def __init__(self, adapter: "VirtualTransportAdapter", channel: ChannelId) -> None:
        self._adapter = adapter
        self._channel = channel
        self._seq = 0

    def on_message_received(self, msg: can.Message) -> None:
        arrival_ns = time.monotonic_ns()
        self._seq += 1
        dlc = msg.dlc if msg.dlc is not None else len(msg.data)
        env = RawFrameEnvelope(
            adapter_epoch=self._adapter.epoch,
            channel=self._channel,
            device_timestamp=None,  # virtual: no HW timestamp (capability Unknown)
            backend_arrival_ns=arrival_ns,
            can_id=msg.arbitration_id,
            is_extended=bool(msg.is_extended_id),
            is_remote=bool(msg.is_remote_frame),
            dlc=dlc,
            data=bytes(msg.data[:dlc]),  # respect DLC; never adapter padding
            channel_sequence=self._seq,
            direction=Direction.RX,
            source=FrameSource.VIRTUAL,
        )
        self._adapter._on_frame(self._channel, env)

    def on_error(self, exc: Exception) -> None:  # pragma: no cover - defensive
        self._adapter._on_error(self._channel, exc)


class VirtualTransportAdapter:
    """python-can virtual dual-bus adapter (High + Low)."""

    def __init__(self, rx_queue_maxsize: int = 65536) -> None:
        self._epoch = 0
        self._health = AdapterHealth.ABSENT
        self._maxsize = rx_queue_maxsize
        self._lock = threading.Lock()

        self._queue: "queue.Queue[RawFrameEnvelope]" = queue.Queue(maxsize=rx_queue_maxsize)
        self._rx_buses: dict[ChannelId, can.BusABC] = {}
        self._emitters: dict[ChannelId, can.BusABC] = {}
        self._notifiers: dict[ChannelId, can.Notifier] = {}
        self._channel_names: dict[ChannelId, str] = {}
        self._state: dict[ChannelId, ChannelState] = {
            ch: ChannelState(channel=ch.value) for ch in _CHANNELS
        }
        self._overflow = 0
        self._queue_high_water = 0

    # ---- capability & status -------------------------------------------------

    @property
    def epoch(self) -> int:
        return self._epoch

    @property
    def capability(self) -> Capability:
        # Virtual bus exposes none of these; all Unknown/False, never faked.
        return Capability(
            hw_timestamps=False,
            tx_echo=False,
            listen_only=None,
            bus_off_reporting=False,
            tec_rec_reporting=False,
        )

    def status(self) -> AdapterStatus:
        with self._lock:
            channels = {ch.value: self._state[ch].model_copy() for ch in _CHANNELS}
        return AdapterStatus(
            identity="virtual",
            health=self._health,
            adapter_epoch=self._epoch,
            capability=self.capability,
            channels=channels,
        )

    # ---- lifecycle -----------------------------------------------------------

    def open(self) -> None:
        if self._health in (AdapterHealth.OPEN, AdapterHealth.ACTIVE):
            return
        self._epoch += 1
        suffix = uuid.uuid4().hex[:8]
        for ch in _CHANNELS:
            name = f"etrike_{ch.value}_{suffix}"
            self._channel_names[ch] = name
            # Adapter's receive bus: does not hear its own sends (no TX echo).
            rx = can.Bus(interface="virtual", channel=name, receive_own_messages=False)
            # Emitter bus on the same channel: injection / synthetic / TX source.
            tx = can.Bus(interface="virtual", channel=name, receive_own_messages=False)
            self._rx_buses[ch] = rx
            self._emitters[ch] = tx
            # Short recv timeout so the reader thread wakes frequently and
            # close()/stop() returns promptly (delivery itself is event-driven).
            self._notifiers[ch] = can.Notifier(
                rx, [_QueueListener(self, ch)], timeout=0.1
            )
        self._health = AdapterHealth.OPEN

    def close(self) -> None:
        for ch in list(self._notifiers):
            try:
                self._notifiers[ch].stop()
            except Exception:  # pragma: no cover - best-effort shutdown
                pass
        for bus_map in (self._emitters, self._rx_buses):
            for ch in list(bus_map):
                try:
                    bus_map[ch].shutdown()
                except Exception:  # pragma: no cover
                    pass
        self._notifiers.clear()
        self._rx_buses.clear()
        self._emitters.clear()
        self._health = AdapterHealth.CLOSED

    # ---- receive -------------------------------------------------------------

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

    def _on_frame(self, channel: ChannelId, env: RawFrameEnvelope) -> None:
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
            if self._health == AdapterHealth.OPEN:
                self._health = AdapterHealth.ACTIVE

    def _on_error(self, channel: ChannelId, exc: Exception) -> None:  # pragma: no cover
        with self._lock:
            self._state[channel].activity = ChannelActivity.QUIET
            self._health = AdapterHealth.DEGRADED

    # ---- transmit / inject ---------------------------------------------------

    def inject(
        self,
        channel: ChannelId,
        can_id: int,
        data: bytes = b"",
        *,
        is_extended: bool = False,
        is_remote: bool = False,
    ) -> None:
        """Publish a frame onto the virtual channel so the adapter observes it.

        Used by tests, the injection API, and synthetic peers in Pure Software.
        """
        emitter = self._emitters.get(channel)
        if emitter is None:
            raise RuntimeError("adapter not open")
        emitter.send(
            can.Message(
                arbitration_id=can_id,
                is_extended_id=is_extended,
                is_remote_frame=is_remote,
                dlc=len(data),
                data=bytes(data),
            )
        )

    def send(self, frame: RawFrameEnvelope) -> str:
        """Transmit a raw envelope. On the virtual bus this is the same as inject;
        source/direction labeling stays with the caller's envelope."""
        self.inject(
            frame.channel,
            frame.can_id,
            frame.data,
            is_extended=frame.is_extended,
            is_remote=frame.is_remote,
        )
        return "submitted"
