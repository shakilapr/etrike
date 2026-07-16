"""Receive pipeline / router (workplan §1.4).

Deterministic tests: drive the router's synchronous ``drain_once`` directly (no
async loop) so timing is bounded by the virtual notifier only.
"""

from __future__ import annotations

import pytest

from vtc import protocol_bridge as proto
from vtc.models.frames import ChannelId, RawFrameEnvelope
from vtc.models.state import FreshnessState
from vtc.pipeline.router import Router
from vtc.state.latest import LatestStore
from vtc.transport.virtual import VirtualTransportAdapter


def _envelope(channel: ChannelId, can_id: int, data: bytes, arrival_ns: int, seq: int):
    return RawFrameEnvelope(
        adapter_epoch=1,
        channel=channel,
        backend_arrival_ns=arrival_ns,
        can_id=can_id,
        dlc=len(data),
        data=data,
        channel_sequence=seq,
    )


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
    assert m.signals["alive_ctr"].raw_value == 255  # unscaled: raw == engineering
    assert m.signals["can_ok"].engineering_value == 1
    assert router.sequence >= 1


def test_observed_rate_from_arrival_spacing():
    # Feed the router hand-built envelopes 100ms apart -> 10 Hz observed.
    latest = LatestStore()
    router = Router(transport=None, latest=latest)
    for i in range(3):
        router.process(
            _envelope(ChannelId.LOW, 0x7FE, bytes.fromhex("ffff"), i * 100_000_000, i)
        )
    m = {(x.bus, x.can_id): x for x in latest.snapshot().messages}[("low", 0x7FE)]
    assert m.observed_rate_hz == pytest.approx(10.0)


def test_raw_value_reverses_scaling():
    # STEER_DIAG angle_0_1deg: factor 0.1, offset -3000 -> engineering 0.0 == raw 30000.
    latest = LatestStore()
    router = Router(transport=None, latest=latest)
    status, frame = proto.encode(
        "rt:steer_diag",
        {
            "angle_0_1deg": 0.0,
            "fault": 0,
            "motor_current": 0.0,
            "ecu_temp": 0.0,
            "reserved": 0,
        },
        bus="high",
    )
    assert status == "ok"
    router.process(_envelope(ChannelId.HIGH, 0x310, frame.data, 0, 0))

    m = {(x.bus, x.can_id): x for x in latest.snapshot().messages}[("high", 0x310)]
    angle = m.signals["angle_0_1deg"]
    assert angle.engineering_value == pytest.approx(0.0)
    assert angle.raw_value == 30000
    assert m.signals["fault"].raw_value == 0  # unscaled field


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
