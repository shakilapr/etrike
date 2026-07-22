"""CANalyst-II wrapper contract without physical USB hardware."""

from __future__ import annotations

import queue
import time
from typing import Any

import can

from control_toolkit.config import Profile
from control_toolkit.models.adapter import AdapterHealth
from control_toolkit.models.frames import (
    ChannelId,
    Direction,
    FrameSource,
    RawFrameEnvelope,
)
from control_toolkit.models.session import (
    BenchTxState,
    ChangeProfileRequest,
    CreateSessionRequest,
)
from control_toolkit.services.ownership import OwnershipTable
from control_toolkit.services.session_manager import SessionManager
from control_toolkit.transport.canalyst import (
    CanalystTransportAdapter,
    discover_canalyst,
)


class FakeBus:
    def __init__(self, **kwargs: Any) -> None:
        self.kwargs = kwargs
        self.rx: queue.Queue[Any] = queue.Queue()
        self.sent: list[tuple[can.Message, float | None]] = []
        self.shutdown_called = False
        self.send_error: Exception | None = None
        self.RX_POLL_DELAY = 0.020

    def recv(self, timeout: float | None = None) -> can.Message | None:
        try:
            item = self.rx.get(timeout=timeout)
        except queue.Empty:
            return None
        if isinstance(item, BaseException):
            raise item
        return item

    def send(self, msg: can.Message, timeout: float | None = None) -> None:
        if self.send_error is not None:
            raise self.send_error
        self.sent.append((msg, timeout))

    def shutdown(self) -> None:
        self.shutdown_called = True


class FakeFactory:
    def __init__(self) -> None:
        self.calls: list[dict[str, Any]] = []
        self.buses: list[FakeBus] = []

    def __call__(self, **kwargs: Any) -> FakeBus:
        self.calls.append(kwargs)
        bus = FakeBus(**kwargs)
        self.buses.append(bus)
        return bus


def _adapter(factory: FakeFactory, **kwargs: Any) -> CanalystTransportAdapter:
    return CanalystTransportAdapter(
        bus_factory=factory,
        poll_ms=2,
        receive_timeout_ms=10,
        reconnect_initial_ms=10,
        reconnect_max_ms=20,
        recovery_stability_ms=0,
        jitter_ratio=0,
        **kwargs,
    )


def _drain(adapter: CanalystTransportAdapter, count: int, timeout_s: float = 1.0):
    frames = []
    deadline = time.monotonic() + timeout_s
    while len(frames) < count and time.monotonic() < deadline:
        frames.extend(adapter.poll(timeout=0.02))
    return frames


def _tx_frame(channel: ChannelId, *, dlc: int = 2) -> RawFrameEnvelope:
    return RawFrameEnvelope(
        adapter_epoch=1,
        channel=channel,
        backend_arrival_ns=time.monotonic_ns(),
        can_id=0x321,
        dlc=dlc,
        data=bytes(range(dlc)),
        channel_sequence=0,
        direction=Direction.TX,
        source=FrameSource.INJECTION,
    )


def test_discovery_proves_driver_open_and_closes_probe() -> None:
    factory = FakeFactory()
    result = discover_canalyst(force=True, bus_factory=factory)

    assert result.available is True
    assert result.device_index == 0
    assert result.bitrate == 500_000
    assert factory.calls[0]["interface"] == "canalystii"
    assert factory.calls[0]["channel"] == 0
    assert factory.buses[0].shutdown_called is True


def test_open_configures_both_channels_and_tuned_poll_delay() -> None:
    factory = FakeFactory()
    adapter = _adapter(factory)
    adapter.open()
    try:
        assert factory.calls[0] == {
            "interface": "canalystii",
            "channel": (0, 1),
            "bitrate": 500_000,
            "device": 0,
            "rx_queue_size": None,
        }
        assert factory.buses[0].RX_POLL_DELAY == 0.002
        status = adapter.status()
        assert status.health is AdapterHealth.OPEN
        assert status.channel_map == {"high": 0, "low": 1}
        assert status.worker_alive is True
        assert status.capability.hw_timestamps is False
        assert status.capability.tx_echo is None
        assert status.capability.bus_off_reporting is None
    finally:
        adapter.close()
    assert factory.buses[0].shutdown_called is True
    assert adapter.status().health is AdapterHealth.CLOSED


def test_dual_channel_receive_slices_dlc_and_ignores_device_timestamp() -> None:
    factory = FakeFactory()
    adapter = _adapter(factory)
    adapter.open()
    bus = factory.buses[0]
    try:
        bus.rx.put(
            can.Message(
                timestamp=123.456,
                channel=0,
                arbitration_id=0x300,
                dlc=2,
                data=b"\xAA\xBB",
            )
        )
        bus.rx.put(
            can.Message(
                timestamp=999.0,
                channel=1,
                arbitration_id=0x204,
                dlc=5,
                data=b"\x01\x02\x03\x04\x05",
            )
        )
        frames = _drain(adapter, 2)
        assert len(frames) == 2
        by_id = {f.can_id: f for f in frames}
        assert by_id[0x300].channel is ChannelId.HIGH
        assert by_id[0x204].channel is ChannelId.LOW
        assert by_id[0x300].data == b"\xAA\xBB"
        assert by_id[0x204].dlc == 5
        assert all(f.device_timestamp is None for f in frames)
        assert all(f.source is FrameSource.PHYSICAL for f in frames)
        assert by_id[0x300].channel_sequence == 1
        assert by_id[0x204].channel_sequence == 1
    finally:
        adapter.close()


def test_remote_frame_keeps_requested_dlc_without_payload() -> None:
    factory = FakeFactory()
    adapter = _adapter(factory)
    adapter.open()
    try:
        factory.buses[0].rx.put(
            can.Message(
                channel=1,
                arbitration_id=0x555,
                is_remote_frame=True,
                dlc=4,
            )
        )
        frame = _drain(adapter, 1)[0]
        assert frame.is_remote is True
        assert frame.dlc == 4
        assert frame.data == b""
    finally:
        adapter.close()


def test_transmit_maps_high_low_and_counts_submission() -> None:
    factory = FakeFactory()
    adapter = _adapter(factory)
    adapter.open()
    try:
        assert adapter.send(_tx_frame(ChannelId.HIGH)) == "submitted"
        assert adapter.send(_tx_frame(ChannelId.LOW)) == "submitted"
        sent = factory.buses[0].sent
        assert [item[0].channel for item in sent] == [0, 1]
        assert all(item[1] == 0.1 for item in sent)
        status = adapter.status()
        assert status.channels["high"].tx_count == 1
        assert status.channels["low"].tx_count == 1
        # Software TX mirror for observation (no hardware echo).
        mirrored = _drain(adapter, 2)
        assert len(mirrored) == 2
        assert all(f.direction is Direction.TX for f in mirrored)
        assert {f.channel for f in mirrored} == {ChannelId.HIGH, ChannelId.LOW}
        # Mirror must not inflate true RX counters.
        status2 = adapter.status()
        assert status2.channels["high"].rx_count == 0
        assert status2.channels["low"].rx_count == 0
    finally:
        adapter.close()


def test_application_queue_overflow_is_counted_per_channel() -> None:
    factory = FakeFactory()
    adapter = _adapter(factory, rx_queue_maxsize=1)
    env = _tx_frame(ChannelId.HIGH)
    adapter._enqueue(ChannelId.HIGH, env)  # transport boundary accounting
    adapter._enqueue(ChannelId.HIGH, env)

    status = adapter.status()
    assert status.channels["high"].rx_count == 1
    assert status.channels["high"].rx_overflow == 1
    assert status.channels["low"].rx_overflow == 0


def test_transport_failure_neutralizes_physical_session_and_recovery_stays_rx_only() -> None:
    ownership = OwnershipTable()
    epoch = 7
    manager = SessionManager(
        ownership=ownership,
        get_transport_open=lambda: True,
        get_adapter_epoch=lambda: epoch,
        physical_available=lambda: (True, "fake adapter"),
    )
    session = manager.create(CreateSessionRequest(profile=Profile.BENCH_TEST))
    session = manager.set_bench_tx(True, session.revision)
    ownership.claim(bus="low", can_id=0x204, owner="test")

    failed = manager.transport_failed("USB removed")
    assert failed.bench_tx is BenchTxState.DISABLED
    assert failed.leases == []
    assert "transport_failed" in failed.capabilities

    recovered = manager.transport_recovered(8)
    assert recovered.adapter_epoch == 8
    assert recovered.bench_tx is BenchTxState.DISABLED
    assert "transport_recovered" in recovered.capabilities
    assert "transport_failed" not in recovered.capabilities


def test_failed_profile_transport_open_restores_previous_visible_profile() -> None:
    ownership = OwnershipTable()

    def switch(profile: Profile) -> None:
        if profile is Profile.BENCH_TEST:
            raise RuntimeError("fake USB open failure")

    manager = SessionManager(
        ownership=ownership,
        get_transport_open=lambda: True,
        get_adapter_epoch=lambda: 1,
        physical_available=lambda: (True, "probe succeeded"),
        on_profile_change=switch,
    )
    current = manager.create(CreateSessionRequest(profile=Profile.PURE_SOFTWARE))

    try:
        manager.change_profile(
            ChangeProfileRequest(
                profile=Profile.BENCH_TEST,
                confirm=True,
                expected_revision=current.revision,
            )
        )
    except Exception as exc:
        assert "fake USB open failure" in str(exc)
    else:  # pragma: no cover - assertion guard
        raise AssertionError("profile switch unexpectedly succeeded")

    restored = manager.snapshot()
    assert restored.profile is Profile.PURE_SOFTWARE
    assert restored.destination == "virtual"
    assert restored.session_id == current.session_id
    assert restored.bench_tx is BenchTxState.DISABLED
