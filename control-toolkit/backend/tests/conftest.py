import queue
from typing import Any
import can
import pytest
from fastapi.testclient import TestClient

from control_toolkit.config import ToolkitConfig
from control_toolkit.main import create_app


class LoopbackBus:
    def __init__(self, **kwargs: Any) -> None:
        self.kwargs = kwargs
        self.rx: queue.Queue[Any] = queue.Queue()
        self.sent: list[tuple[can.Message, float | None]] = []
        self.shutdown_called = False
        self.send_error: Exception | None = None
        self.RX_POLL_DELAY = 0.005

    def recv(self, timeout: float | None = None) -> can.Message | None:
        try:
            item = self.rx.get(timeout=timeout or 0.01)
        except queue.Empty:
            return None
        if isinstance(item, BaseException):
            raise item
        return item

    def send(self, msg: can.Message, timeout: float | None = None) -> None:
        if self.send_error is not None:
            raise self.send_error
        self.sent.append((msg, timeout))
        # Echo back as loopback reception on the bus
        self.rx.put(msg)

    def shutdown(self) -> None:
        self.shutdown_called = True


class FakeBusFactory:
    def __init__(self) -> None:
        self.buses: list[LoopbackBus] = []

    def __call__(self, **kwargs: Any) -> LoopbackBus:
        bus = LoopbackBus(**kwargs)
        self.buses.append(bus)
        return bus


@pytest.fixture()
def client() -> TestClient:
    factory = FakeBusFactory()
    app = create_app(ToolkitConfig(), bus_factory=factory)
    with TestClient(app) as c:
        c.test_bus_factory = factory
        yield c


@pytest.fixture()
def unattached_client() -> TestClient:
    """Client with no bus factory and no physical adapter available."""
    def _fail_factory(**kwargs: Any) -> Any:
        raise RuntimeError("CANalyst-II not connected")

    app = create_app(ToolkitConfig(), bus_factory=_fail_factory)
    with TestClient(app) as c:
        yield c
