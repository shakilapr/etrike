"""CANalyst-II disconnect/reconnect state machine."""

from __future__ import annotations

import time
import threading
from typing import Any

import can

from control_toolkit.models.adapter import AdapterHealth
from control_toolkit.transport.canalyst import CanalystTransportAdapter
from tests.test_canalyst_adapter import FakeBus


class BlockingBus(FakeBus):
    def __init__(self) -> None:
        super().__init__()
        self.release = threading.Event()

    def recv(self, timeout: float | None = None) -> can.Message | None:
        self.release.wait(timeout=2.0)
        return None

    def shutdown(self) -> None:
        self.release.set()
        super().shutdown()


class PlannedFactory:
    def __init__(self, plan: list[FakeBus | Exception]) -> None:
        self.plan = list(plan)
        self.calls: list[dict[str, Any]] = []

    def __call__(self, **kwargs: Any) -> FakeBus:
        self.calls.append(kwargs)
        item = self.plan.pop(0)
        if isinstance(item, Exception):
            raise item
        item.kwargs = kwargs
        return item


def _wait_for(predicate, timeout_s: float = 1.5) -> None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.01)
    raise AssertionError("condition not reached before timeout")


def test_receive_failure_reconnects_with_new_epoch_and_reports_callbacks() -> None:
    first = FakeBus()
    second = FakeBus()
    factory = PlannedFactory([first, OSError("still absent"), second])
    failures: list[str] = []
    recovered: list[int] = []
    adapter = CanalystTransportAdapter(
        bus_factory=factory,
        receive_timeout_ms=10,
        reconnect_initial_ms=10,
        reconnect_max_ms=20,
        recovery_stability_ms=0,
        jitter_ratio=0,
        on_failure=failures.append,
        on_recovered=recovered.append,
    )
    adapter.open()
    assert adapter.epoch == 1
    try:
        first.rx.put(can.CanOperationError("USB removed"))
        _wait_for(lambda: adapter.epoch == 2)
        _wait_for(lambda: adapter.status().health is AdapterHealth.OPEN)

        assert first.shutdown_called is True
        assert len(factory.calls) == 3
        assert failures and "USB removed" in failures[0]
        assert recovered == [2]
        status = adapter.status()
        assert status.retry_count == 0
        assert status.last_error is None
    finally:
        adapter.close()


def test_send_failure_requests_reconnect_and_rejects_during_recovery() -> None:
    first = FakeBus()
    second = FakeBus()
    factory = PlannedFactory([first, second])
    failures: list[str] = []
    adapter = CanalystTransportAdapter(
        bus_factory=factory,
        receive_timeout_ms=10,
        reconnect_initial_ms=10,
        reconnect_max_ms=10,
        recovery_stability_ms=200,
        jitter_ratio=0,
        on_failure=failures.append,
    )
    adapter.open()
    try:
        from tests.test_canalyst_adapter import _tx_frame
        from control_toolkit.models.frames import ChannelId

        first.send_error = OSError("device gone")
        assert adapter.send(_tx_frame(ChannelId.HIGH)) == "rejected"
        assert adapter.status().health is AdapterHealth.DEGRADED
        _wait_for(lambda: adapter.epoch == 2)
        assert adapter.status().health is AdapterHealth.RECOVERING
        # Reconnect is intentionally receive-only during the stability window.
        assert adapter.send(_tx_frame(ChannelId.HIGH)) == "rejected"
        assert failures and "device gone" in failures[0]
    finally:
        adapter.close()


def test_worker_heartbeat_timeout_disables_transport_and_requests_reconnect() -> None:
    first = BlockingBus()
    second = FakeBus()
    factory = PlannedFactory([first, second])
    failures: list[str] = []
    adapter = CanalystTransportAdapter(
        bus_factory=factory,
        receive_timeout_ms=10,
        reconnect_initial_ms=10,
        reconnect_max_ms=10,
        recovery_stability_ms=0,
        worker_degraded_ms=100,
        worker_failed_ms=150,
        jitter_ratio=0,
        on_failure=failures.append,
    )
    adapter.open()
    try:
        _wait_for(lambda: bool(failures))
        assert "heartbeat lost" in failures[0]
        assert adapter.status().health is AdapterHealth.DEGRADED

        # Let the blocked driver call return; reconnect must use a new epoch.
        first.release.set()
        _wait_for(lambda: adapter.epoch == 2)
        _wait_for(lambda: adapter.status().health is AdapterHealth.OPEN)
    finally:
        adapter.close()
