"""CANalyst-II physical transport (architecture §4.4 / workplan Phase 2).

Channel 0 is the E-Trike High bus and channel 1 is the Low bus; both are
500 kbit/s by default.  The adapter wraps python-can's unofficial CANalyst-II
backend with the application guarantees it does not provide itself:

* tuned polling (the upstream default is 20 ms);
* an instrumented bounded application RX queue;
* exact DLC slicing and per-channel sequence numbers;
* worker heartbeat, visible failures, and bounded reconnect backoff;
* a new adapter epoch after reconnect; and
* callbacks that let the lifecycle disable Bench TX on any transport failure.

The CANalyst hardware timestamp is deliberately not placed in the envelope.
Its rollover/epoch is not required for this operator UI; cross-channel analysis
uses the monotonic backend arrival timestamp and documents USB grouping jitter.
"""

from __future__ import annotations

import queue
import random
import logging
import threading
import time
from dataclasses import asdict, dataclass
from typing import Any, Callable

import can

from control_toolkit.models.adapter import (
    AdapterHealth,
    AdapterStatus,
    Capability,
    ChannelActivity,
    ChannelState,
)
from control_toolkit.models.frames import ChannelId, Direction, FrameSource, RawFrameEnvelope

# Architecture: CH0 High, CH1 Low (the old debug-tool defaults were reversed).
_HW_TO_BUS: dict[int, ChannelId] = {0: ChannelId.HIGH, 1: ChannelId.LOW}
_BUS_TO_HW: dict[ChannelId, int] = {v: k for k, v in _HW_TO_BUS.items()}
_USB_VID = 0x04D8
_USB_PID = 0x0053

BusFactory = Callable[..., Any]
FailureCallback = Callable[[str], None]
RecoveredCallback = Callable[[int], None]


@dataclass(frozen=True, slots=True)
class CanalystDiscovery:
    available: bool
    reason: str
    device_index: int
    bitrate: int
    usb_vid: int = _USB_VID
    usb_pid: int = _USB_PID
    usb_visible: bool | None = None
    python_can_version: str = can.__version__
    backend: str = "python-can/canalystii"

    def model_dump(self) -> dict[str, Any]:
        """Pydantic-like helper used by API payload builders."""
        return asdict(self)


_probe_cache: dict[tuple[int, int], tuple[float, CanalystDiscovery]] = {}
_PROBE_TTL_S = 5.0


def discover_canalyst(
    device_index: int = 0,
    bitrate: int = 500_000,
    *,
    force: bool = False,
    bus_factory: BusFactory | None = None,
) -> CanalystDiscovery:
    """Probe USB visibility and prove that python-can can open channel 0.

    A USB VID/PID match alone is insufficient: a device with the vendor driver
    still bound can be visible to Windows while unavailable to PyUSB.  The
    short open therefore remains the authoritative availability check.
    Results from the production factory are cached briefly because the Settings
    page polls this endpoint.
    """

    key = (device_index, bitrate)
    now = time.monotonic()
    use_cache = bus_factory is None
    if use_cache and not force:
        cached = _probe_cache.get(key)
        if cached is not None and now - cached[0] < _PROBE_TTL_S:
            return cached[1]

    usb_visible: bool | None = None
    try:
        import usb.core

        usb_visible = bool(
            usb.core.find(idVendor=_USB_VID, idProduct=_USB_PID, find_all=False)
        )
    except Exception:  # PyUSB/backend diagnostics are reported by the open below.
        usb_visible = None

    factory = bus_factory or can.Bus
    bus: Any | None = None
    # python-can logs a misleading "not properly shut down" warning when a bus
    # constructor fails before returning the partially initialized object.  The
    # actual exception is preserved below as the probe reason; suppress only
    # that constructor-cleanup warning during this short probe.
    can_bus_log = logging.getLogger("can.bus")
    previous_log_level = can_bus_log.level
    try:
        can_bus_log.setLevel(logging.ERROR)
        bus = factory(
            interface="canalystii",
            channel=0,
            bitrate=bitrate,
            device=device_index,
            rx_queue_size=None,
        )
        result = CanalystDiscovery(
            available=True,
            reason="CANalyst-II detected and opened through python-can",
            device_index=device_index,
            bitrate=bitrate,
            usb_visible=usb_visible,
        )
    except Exception as exc:  # noqa: BLE001 - hardware drivers vary exception type
        detail = str(exc).strip() or type(exc).__name__
        if usb_visible is False:
            reason = (
                "CANalyst-II USB device 04D8:0053 not found; connect USB only "
                "before attaching either CAN bus"
            )
        else:
            reason = f"CANalyst-II cannot be opened through python-can: {detail}"
        result = CanalystDiscovery(
            available=False,
            reason=reason,
            device_index=device_index,
            bitrate=bitrate,
            usb_visible=usb_visible,
        )
    finally:
        can_bus_log.setLevel(previous_log_level)
        if bus is not None:
            try:
                bus.shutdown()
            except Exception:
                pass

    if use_cache:
        _probe_cache[key] = (time.monotonic(), result)
    return result


def canalyst_available(
    device_index: int = 0, bitrate: int = 500_000, *, force: bool = False
) -> tuple[bool, str]:
    """Compatibility wrapper for session/profile availability checks."""
    result = discover_canalyst(device_index, bitrate, force=force)
    return result.available, result.reason


class CanalystTransportAdapter:
    """Dual-channel CANalyst-II adapter implementing the Transport protocol."""

    def __init__(
        self,
        *,
        rx_queue_maxsize: int = 65_536,
        bitrate: int = 500_000,
        device_index: int = 0,
        poll_ms: float = 2.0,
        receive_timeout_ms: float = 100.0,
        reconnect_initial_ms: float = 250.0,
        reconnect_max_ms: float = 5_000.0,
        recovery_stability_ms: float = 500.0,
        worker_degraded_ms: float = 500.0,
        worker_failed_ms: float = 1_500.0,
        quiet_after_ms: float = 750.0,
        jitter_ratio: float = 0.20,
        bus_factory: BusFactory | None = None,
        on_failure: FailureCallback | None = None,
        on_recovered: RecoveredCallback | None = None,
    ) -> None:
        self._epoch = 0
        self._health = AdapterHealth.ABSENT
        self._bitrate = int(bitrate)
        self._device_index = int(device_index)
        self._poll_s = max(float(poll_ms), 1.0) / 1000.0
        self._receive_timeout_s = max(float(receive_timeout_ms), 10.0) / 1000.0
        self._reconnect_initial_s = max(float(reconnect_initial_ms), 10.0) / 1000.0
        self._reconnect_max_s = max(
            float(reconnect_max_ms) / 1000.0, self._reconnect_initial_s
        )
        self._recovery_stability_s = max(float(recovery_stability_ms), 0.0) / 1000.0
        worker_degraded_ms = max(worker_degraded_ms, 100.0)
        worker_failed_ms = max(worker_failed_ms, worker_degraded_ms)
        self._worker_degraded_ns = int(worker_degraded_ms * 1_000_000)
        self._worker_failed_ns = int(worker_failed_ms * 1_000_000)
        self._quiet_after_ns = int(max(quiet_after_ms, 100.0) * 1_000_000)
        self._jitter_ratio = max(0.0, min(float(jitter_ratio), 0.5))
        self._bus_factory = bus_factory or can.Bus
        self._on_failure = on_failure
        self._on_recovered = on_recovered

        self._lock = threading.RLock()
        self._queue: queue.Queue[RawFrameEnvelope] = queue.Queue(maxsize=rx_queue_maxsize)
        self._bus: Any | None = None
        self._thread: threading.Thread | None = None
        self._monitor_thread: threading.Thread | None = None
        self._stop = threading.Event()
        self._reconnect_requested = threading.Event()
        self._state: dict[ChannelId, ChannelState] = {
            ch: ChannelState(channel=ch.value) for ch in _BUS_TO_HW
        }
        self._seq: dict[ChannelId, int] = {ch: 0 for ch in _BUS_TO_HW}
        self._queue_high_water = 0
        self._last_error: str | None = None
        self._worker_heartbeat_ns: int | None = None
        self._retry_count = 0
        self._recovery_since: float | None = None
        self._failure_notified = False

    @property
    def epoch(self) -> int:
        with self._lock:
            return self._epoch

    @property
    def capability(self) -> Capability:
        # The device carries timestamps, but this application intentionally
        # ignores them.  CANalyst-II exposes no trustworthy bus-off/TEC/REC,
        # TX-echo, or listen-only evidence through this backend.
        return Capability(
            hw_timestamps=False,
            tx_echo=None,
            listen_only=None,
            bus_off_reporting=None,
            tec_rec_reporting=None,
        )

    def status(self) -> AdapterStatus:
        with self._lock:
            channels = {ch.value: st.model_copy() for ch, st in self._state.items()}
            health = self._health
            heartbeat = self._worker_heartbeat_ns
            worker_alive = self._thread is not None and self._thread.is_alive()
            if (
                not self._stop.is_set()
                and self._thread is not None
                and not worker_alive
                and health not in (AdapterHealth.ABSENT, AdapterHealth.CLOSED)
            ):
                health = AdapterHealth.DEGRADED
            return AdapterStatus(
                identity="canalystii",
                health=health,
                adapter_epoch=self._epoch,
                capability=self.capability,
                channels=channels,
                device_index=self._device_index,
                bitrate=self._bitrate,
                channel_map={"high": 0, "low": 1},
                worker_alive=worker_alive,
                worker_heartbeat_ns=heartbeat,
                retry_count=self._retry_count,
                last_error=self._last_error,
            )

    def open(self) -> None:
        """Open both channels synchronously, then start RX/reconnect workers."""
        with self._lock:
            if self._health in (
                AdapterHealth.OPEN,
                AdapterHealth.ACTIVE,
                AdapterHealth.QUIET,
                AdapterHealth.RECOVERING,
            ):
                return
            self._health = AdapterHealth.OPENING
            self._last_error = None
        self._stop.clear()
        self._reconnect_requested.clear()
        self._drain_queue()

        bus, detail = self._try_make_bus()
        if bus is None:
            assert detail is not None
            with self._lock:
                self._health = AdapterHealth.ABSENT
                self._last_error = detail
            raise RuntimeError(f"CANalyst-II open failed: {detail}")

        with self._lock:
            self._bus = bus
            self._epoch += 1
            self._reset_epoch_state_locked()
            self._health = AdapterHealth.OPEN
            self._worker_heartbeat_ns = time.monotonic_ns()
            self._retry_count = 0
            self._failure_notified = False

        self._thread = threading.Thread(
            target=self._rx_loop,
            name="canalyst-rx",
            daemon=True,
        )
        self._monitor_thread = threading.Thread(
            target=self._monitor_loop,
            name="canalyst-health",
            daemon=True,
        )
        self._thread.start()
        self._monitor_thread.start()

    def _make_bus(self) -> Any:
        bus = self._bus_factory(
            interface="canalystii",
            channel=(0, 1),
            bitrate=self._bitrate,
            device=self._device_index,
            # Never use deque(maxlen=N): upstream would silently evict old RX.
            rx_queue_size=None,
        )
        # python-can's CANalyst backend reads this attribute for its polling
        # sleep.  Setting it on the instance avoids a process-global patch.
        setattr(bus, "RX_POLL_DELAY", self._poll_s)
        return bus

    def _try_make_bus(self) -> tuple[Any | None, str | None]:
        """Open without python-can's failed-constructor cleanup warning."""
        can_bus_log = logging.getLogger("can.bus")
        previous_log_level = can_bus_log.level
        try:
            can_bus_log.setLevel(logging.ERROR)
            try:
                return self._make_bus(), None
            except Exception as exc:  # noqa: BLE001
                return None, str(exc).strip() or type(exc).__name__
        finally:
            can_bus_log.setLevel(previous_log_level)

    def _rx_loop(self) -> None:
        try:
            while not self._stop.is_set():
                with self._lock:
                    bus = self._bus

                if bus is None:
                    if not self._attempt_reconnect():
                        break
                    continue

                if self._reconnect_requested.is_set():
                    self._disconnect_bus(bus)
                    continue

                self._touch_worker()
                try:
                    msg = bus.recv(timeout=self._receive_timeout_s)
                except Exception as exc:  # noqa: BLE001
                    self._signal_failure(
                        f"CANalyst-II receive failed: {str(exc).strip() or type(exc).__name__}"
                    )
                    self._disconnect_bus(bus)
                    continue
                self._touch_worker()

                if self._reconnect_requested.is_set():
                    self._disconnect_bus(bus)
                    continue

                self._finish_recovery_if_stable()
                if msg is None:
                    self._mark_quiet_channels()
                    continue
                self._handle_message(msg)
        finally:
            with self._lock:
                bus = self._bus
            if bus is not None:
                self._disconnect_bus(bus)

    def _monitor_loop(self) -> None:
        interval = min(0.25, max(0.05, self._receive_timeout_s))
        while not self._stop.wait(interval):
            now_ns = time.monotonic_ns()
            with self._lock:
                heartbeat = self._worker_heartbeat_ns
                health = self._health
            if heartbeat is None or health in (
                AdapterHealth.ABSENT,
                AdapterHealth.CLOSED,
                AdapterHealth.RECOVERING,
            ):
                continue
            age_ns = now_ns - heartbeat
            if age_ns >= self._worker_failed_ns:
                self._signal_failure(
                    f"CANalyst-II receive worker heartbeat lost for {age_ns / 1_000_000:.0f} ms"
                )
            elif age_ns >= self._worker_degraded_ns:
                with self._lock:
                    if self._health in (
                        AdapterHealth.OPEN,
                        AdapterHealth.ACTIVE,
                        AdapterHealth.QUIET,
                    ):
                        self._health = AdapterHealth.DEGRADED
            else:
                # A slow USB call can briefly cross the degraded threshold.
                # Restore ordinary receive health when heartbeats resume, but
                # never mask a real send/receive failure awaiting reconnect.
                with self._lock:
                    if (
                        self._health is AdapterHealth.DEGRADED
                        and not self._reconnect_requested.is_set()
                        and not self._failure_notified
                    ):
                        any_rx = any(
                            st.activity is ChannelActivity.ACTIVE
                            for st in self._state.values()
                        )
                        self._health = (
                            AdapterHealth.ACTIVE if any_rx else AdapterHealth.OPEN
                        )

    def _attempt_reconnect(self) -> bool:
        while not self._stop.is_set():
            with self._lock:
                self._retry_count += 1
                attempt = self._retry_count
                self._health = AdapterHealth.RECOVERING
            base = min(
                self._reconnect_initial_s * (2 ** min(attempt - 1, 8)),
                self._reconnect_max_s,
            )
            jitter = random.uniform(-self._jitter_ratio, self._jitter_ratio) * base
            if self._stop.wait(max(0.0, base + jitter)):
                return False
            bus, detail = self._try_make_bus()
            if bus is None:
                assert detail is not None
                with self._lock:
                    self._health = AdapterHealth.DEGRADED
                    self._last_error = f"reconnect {attempt} failed: {detail}"
                    self._worker_heartbeat_ns = time.monotonic_ns()
                continue

            self._drain_queue()
            with self._lock:
                self._bus = bus
                self._epoch += 1
                self._reset_epoch_state_locked()
                self._health = AdapterHealth.RECOVERING
                self._last_error = None
                self._recovery_since = time.monotonic()
                self._worker_heartbeat_ns = time.monotonic_ns()
                self._reconnect_requested.clear()
            return True
        return False

    def _finish_recovery_if_stable(self) -> None:
        callback: RecoveredCallback | None = None
        epoch = 0
        with self._lock:
            if self._health is not AdapterHealth.RECOVERING:
                return
            since = self._recovery_since
            if since is None or time.monotonic() - since < self._recovery_stability_s:
                return
            self._health = AdapterHealth.OPEN
            self._retry_count = 0
            self._last_error = None
            self._recovery_since = None
            self._failure_notified = False
            epoch = self._epoch
            callback = self._on_recovered
        if callback is not None:
            try:
                callback(epoch)
            except Exception:
                pass

    def _signal_failure(self, reason: str) -> None:
        callback: FailureCallback | None = None
        with self._lock:
            self._health = AdapterHealth.DEGRADED
            self._last_error = reason
            self._reconnect_requested.set()
            if not self._failure_notified:
                self._failure_notified = True
                callback = self._on_failure
        if callback is not None:
            try:
                callback(reason)
            except Exception:
                pass

    def _disconnect_bus(self, expected: Any) -> None:
        with self._lock:
            if self._bus is not expected:
                return
            self._bus = None
        try:
            expected.shutdown()
        except Exception:
            pass

    def _touch_worker(self) -> None:
        with self._lock:
            self._worker_heartbeat_ns = time.monotonic_ns()

    def _handle_message(self, msg: Any) -> None:
        hw_ch = getattr(msg, "channel", None)
        try:
            hw_ch_i = int(hw_ch)
        except (TypeError, ValueError):
            return
        channel = _HW_TO_BUS.get(hw_ch_i)
        if channel is None:
            return

        try:
            dlc = int(msg.dlc if msg.dlc is not None else len(msg.data))
            is_remote = bool(msg.is_remote_frame)
            if not 0 <= dlc <= 8:
                raise ValueError(f"invalid classic CAN DLC {dlc}")
            raw_data = bytes(msg.data)
            data = b"" if is_remote else raw_data[:dlc]
            if not is_remote and len(data) != dlc:
                raise ValueError(f"payload length {len(data)} is shorter than DLC {dlc}")
        except Exception as exc:  # noqa: BLE001
            with self._lock:
                st = self._state[channel]
                st.rx_invalid += 1
                st.last_error = str(exc)
            return

        arrival_ns = time.monotonic_ns()
        with self._lock:
            self._seq[channel] += 1
            seq = self._seq[channel]
            epoch = self._epoch
        env = RawFrameEnvelope(
            adapter_epoch=epoch,
            channel=channel,
            # Deliberately ignored; see module docstring/workplan §2.2.
            device_timestamp=None,
            backend_arrival_ns=arrival_ns,
            can_id=int(msg.arbitration_id),
            is_extended=bool(msg.is_extended_id),
            is_remote=is_remote,
            dlc=dlc,
            data=data,
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
                self._state[channel].rx_overflow += 1
            return
        with self._lock:
            st = self._state[channel]
            st.rx_count += 1
            st.last_rx_ns = env.backend_arrival_ns
            st.activity = ChannelActivity.ACTIVE
            qsize = self._queue.qsize()
            self._queue_high_water = max(self._queue_high_water, qsize)
            st.queue_high_water = max(st.queue_high_water, qsize)
            if self._health in (AdapterHealth.OPEN, AdapterHealth.QUIET):
                self._health = AdapterHealth.ACTIVE

    def _mark_quiet_channels(self) -> None:
        now_ns = time.monotonic_ns()
        with self._lock:
            any_active = False
            for st in self._state.values():
                if st.last_rx_ns is not None and now_ns - st.last_rx_ns >= self._quiet_after_ns:
                    st.activity = ChannelActivity.QUIET
                if st.activity is ChannelActivity.ACTIVE:
                    any_active = True
            if not any_active and self._health is AdapterHealth.ACTIVE:
                self._health = AdapterHealth.QUIET

    def _reset_epoch_state_locked(self) -> None:
        self._seq = {ch: 0 for ch in _BUS_TO_HW}
        for st in self._state.values():
            st.activity = ChannelActivity.UNSEEN
            st.last_rx_ns = None
            st.last_error = None

    def _drain_queue(self) -> None:
        while True:
            try:
                self._queue.get_nowait()
            except queue.Empty:
                return

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
        hw = _BUS_TO_HW.get(frame.channel)
        with self._lock:
            bus = self._bus
            health = self._health
        if bus is None or hw is None or health not in (
            AdapterHealth.OPEN,
            AdapterHealth.ACTIVE,
            AdapterHealth.QUIET,
        ):
            return "rejected"
        try:
            bus.send(
                can.Message(
                    arbitration_id=frame.can_id,
                    is_extended_id=frame.is_extended,
                    is_remote_frame=frame.is_remote,
                    dlc=frame.dlc,
                    data=bytes(frame.data),
                    channel=hw,
                ),
                timeout=0.1,
            )
            with self._lock:
                self._state[frame.channel].tx_count += 1
            # CANalyst has no hardware TX echo. Mirror submitted frames into the
            # observation queue so Live CAN / latest state can show HOST_DRIVE etc.
            self._enqueue_tx_mirror(frame)
            return "submitted"
        except Exception as exc:  # noqa: BLE001
            self._signal_failure(
                f"CANalyst-II send failed: {str(exc).strip() or type(exc).__name__}"
            )
            return "rejected"

    def _enqueue_tx_mirror(self, frame: RawFrameEnvelope) -> None:
        """Software observation of our own TX (not hardware loopback).

        Does not increment ``rx_count`` — that stays true bus RX only.
        """
        channel = frame.channel
        arrival_ns = time.monotonic_ns()
        with self._lock:
            self._seq[channel] += 1
            seq = self._seq[channel]
            epoch = self._epoch
        env = frame.model_copy(
            update={
                "adapter_epoch": epoch,
                "backend_arrival_ns": arrival_ns,
                "channel_sequence": seq,
                "direction": Direction.TX,
                "global_sequence": None,
            }
        )
        try:
            self._queue.put_nowait(env)
        except queue.Full:
            # Drop mirror only; wire TX already succeeded. Count as overflow.
            with self._lock:
                self._state[channel].rx_overflow += 1
            return
        with self._lock:
            qsize = self._queue.qsize()
            self._queue_high_water = max(self._queue_high_water, qsize)
            st = self._state[channel]
            st.queue_high_water = self._queue_high_water
            # Activity reflects host participation on the channel.
            st.activity = ChannelActivity.ACTIVE
            if self._health in (AdapterHealth.OPEN, AdapterHealth.QUIET):
                self._health = AdapterHealth.ACTIVE

    def inject(self, *args: Any, **kwargs: Any) -> None:
        raise RuntimeError("inject not supported on physical CANalyst transport")

    def close(self) -> None:
        """Idempotent shutdown; never close the USB bus under a live RX call."""
        self._stop.set()
        self._reconnect_requested.set()
        thread = self._thread
        monitor = self._monitor_thread
        if thread is not None:
            thread.join(timeout=max(1.5, self._receive_timeout_s + 0.5))
        if monitor is not None:
            monitor.join(timeout=0.5)

        worker_alive = thread is not None and thread.is_alive()
        if not worker_alive:
            with self._lock:
                bus = self._bus
            if bus is not None:
                self._disconnect_bus(bus)
            self._thread = None
            self._monitor_thread = None
            with self._lock:
                self._health = AdapterHealth.CLOSED
        else:
            with self._lock:
                self._health = AdapterHealth.DEGRADED
                self._last_error = "CANalyst-II receive worker did not stop; USB handle left intact"
