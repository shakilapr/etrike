"""Receive pipeline / router (workplan §1.4).

Deterministic tests: drive the router's synchronous ``drain_once`` directly (no
async loop) so timing is bounded by the virtual notifier only.
"""

from __future__ import annotations

import pytest

from control_toolkit.models.frames import ChannelId
from control_toolkit.models.state import FreshnessState
from control_toolkit.pipeline.router import Router
from control_toolkit.state.latest import LatestStore
from control_toolkit.transport.virtual import VirtualTransportAdapter


@pytest.fixture()
def rig():
    adapter = VirtualTransportAdapter()
    adapter.open()
    latest = LatestStore()
    router = Router(adapter, latest)
    try:
        yield adapter, latest, router
    finally:
        adapter.close()


def _drain(router: Router, expected: int = 1, tries: int = 25) -> None:
    got = 0
    for _ in range(tries):
        got += router.drain_once(timeout=0.2)
        if got >= expected:
            return
    raise AssertionError(f"router drained {got} frames, expected {expected}")


def _by_name(latest: LatestStore) -> dict:
    return {m.name: m for m in latest.snapshot().messages}


def test_router_populates_latest_state(rig):
    adapter, latest, router = rig
    adapter.inject(ChannelId.LOW, 0x7FE, bytes.fromhex("ffff"))
    _drain(router)

    m = _by_name(latest)["SYS_HEARTBEAT"]
    assert m.key == "sys:sys_heartbeat"
    assert m.freshness is FreshnessState.LIVE
    assert m.validation_status == "ok"
    assert m.expected_rate_hz == pytest.approx(10.0)  # 100ms cycle -> 10 Hz
    assert m.signals["alive_ctr"].engineering_value == 255
    assert m.signals["can_ok"].engineering_value == 1
    assert router.sequence >= 1


def test_unknown_frame_stays_visible_without_values(rig):
    adapter, latest, router = rig
    adapter.inject(ChannelId.LOW, 0x123, bytes.fromhex("0011"))
    _drain(router)

    m = {(x.bus, x.can_id): x for x in latest.snapshot().messages}[("low", 0x123)]
    assert m.key is None
    assert m.name == "UNKNOWN"
    assert m.validation_status == "unknown_id"
    assert m.signals == {}


def test_global_sequence_is_monotonic(rig):
    adapter, latest, router = rig
    adapter.inject(ChannelId.LOW, 0x7FE, bytes.fromhex("ffff"))
    adapter.inject(ChannelId.HIGH, 0x300, bytes(8))
    _drain(router, expected=2)
    assert router.sequence >= 2


def test_enum_label_is_decoded(rig):
    adapter, latest, router = rig
    # HOST_OBSTACLE_DIST distance_mm has enum {4294967295: "clear"}.
    adapter.inject(ChannelId.HIGH, 0x400, bytes.fromhex("ffffffff"))
    _drain(router)

    m = _by_name(latest)["HOST_OBSTACLE_DIST"]
    assert m.signals["distance_mm"].enum_label == "clear"
