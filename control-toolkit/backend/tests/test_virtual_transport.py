"""Virtual transport adapter (workplan §1.3)."""

from __future__ import annotations

import time

import pytest

from control_toolkit.models.adapter import AdapterHealth
from control_toolkit.models.frames import (
    ChannelId,
    Direction,
    FrameSource,
    RawFrameEnvelope,
)
from control_toolkit.pipeline.decoder import decode_envelope
from control_toolkit.transport.virtual import VirtualTransportAdapter


@pytest.fixture()
def adapter():
    a = VirtualTransportAdapter()
    a.open()
    try:
        yield a
    finally:
        a.close()


def _drain(a: VirtualTransportAdapter, expected: int = 1, deadline_s: float = 2.0):
    out = []
    end = time.monotonic() + deadline_s
    while len(out) < expected and time.monotonic() < end:
        out.extend(a.poll(timeout=0.2))
    return out


def test_capability_is_all_unknown_or_false():
    cap = VirtualTransportAdapter().capability
    assert cap.hw_timestamps is False
    assert cap.tx_echo is False
    assert cap.bus_off_reporting is False
    assert cap.tec_rec_reporting is False
    assert cap.listen_only is None  # Unknown, never faked


def test_injected_frame_is_observed_as_raw_rx_envelope(adapter):
    adapter.inject(ChannelId.LOW, 0x7FE, bytes.fromhex("ffff"))
    frames = _drain(adapter, expected=1)
    assert len(frames) == 1
    env = frames[0]
    assert isinstance(env, RawFrameEnvelope)
    assert env.channel is ChannelId.LOW
    assert env.can_id == 0x7FE
    assert env.dlc == 2
    assert env.data == bytes.fromhex("ffff")
    assert env.direction is Direction.RX
    assert env.source is FrameSource.VIRTUAL
    assert env.device_timestamp is None  # virtual has no HW timestamp


def test_channel_mapping_high_and_low_are_independent(adapter):
    adapter.inject(ChannelId.HIGH, 0x300, bytes(8))
    adapter.inject(ChannelId.LOW, 0x204, bytes(5))
    frames = _drain(adapter, expected=2)
    by_id = {f.can_id: f.channel for f in frames}
    assert by_id[0x300] is ChannelId.HIGH
    assert by_id[0x204] is ChannelId.LOW


def test_dlc_zero_event_frame_roundtrips(adapter):
    adapter.inject(ChannelId.LOW, 0x001, b"")  # SAFETY_ESTOP
    frames = _drain(adapter, expected=1)
    assert frames[0].dlc == 0
    assert frames[0].data == b""


def test_overflow_is_counted_never_silent():
    # Deterministic accounting test: a maxsize-2 queue, 5 frames pushed.
    a = VirtualTransportAdapter(rx_queue_maxsize=2)
    env = RawFrameEnvelope(
        adapter_epoch=1,
        channel=ChannelId.LOW,
        backend_arrival_ns=1,
        can_id=0x7FE,
        dlc=0,
        data=b"",
        channel_sequence=0,
    )
    for _ in range(5):
        a._on_frame(ChannelId.LOW, env)
    st = a.status().channels["low"]
    assert st.rx_count == 2  # only what fit
    assert st.rx_overflow == 3  # the rest counted, not silently dropped


def test_close_is_idempotent(adapter):
    adapter.close()
    adapter.close()  # second close must not raise
    assert adapter.status().health is AdapterHealth.CLOSED


def test_injected_frame_decodes_through_pipeline(adapter):
    # §1.3 transport + §1.4 decoder: inject a golden frame, observe it decoded.
    adapter.inject(ChannelId.LOW, 0x7FE, bytes.fromhex("ffff"))
    env = _drain(adapter, expected=1)[0]
    result = decode_envelope(env)
    assert result.key == "sys:sys_heartbeat"
    assert result.status == "ok"
    assert result.signals["alive_ctr"] == 255
    assert result.signals["can_ok"] == 1
