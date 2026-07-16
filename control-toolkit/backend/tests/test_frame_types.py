"""Immutable frame envelope invariants (workplan §1.2)."""

from __future__ import annotations

import pytest
from pydantic import ValidationError

from control_toolkit.models.frames import ChannelId, Direction, FrameSource, RawFrameEnvelope


def _env(**kw) -> RawFrameEnvelope:
    base = dict(
        adapter_epoch=1,
        channel=ChannelId.LOW,
        backend_arrival_ns=123,
        can_id=0x210,
        dlc=6,
        data=bytes(6),
        channel_sequence=0,
    )
    base.update(kw)
    return RawFrameEnvelope(**base)


def test_data_must_match_dlc_exactly():
    with pytest.raises((ValidationError, ValueError)):
        _env(dlc=6, data=bytes(8))  # padded data is rejected


def test_dlc_zero_event_frame_is_valid():
    env = _env(can_id=0x001, dlc=0, data=b"")
    assert env.dlc == 0 and env.data == b""


def test_envelope_is_immutable():
    env = _env()
    with pytest.raises((ValidationError, TypeError, AttributeError)):
        env.can_id = 0x999


def test_defaults_are_rx_physical():
    env = _env()
    assert env.direction is Direction.RX
    assert env.source is FrameSource.PHYSICAL
