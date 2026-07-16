"""Bounded frame history."""

from control_toolkit.models.frames import ChannelId, RawFrameEnvelope
from control_toolkit.state.history import FrameHistory


def _env(seq: int, can_id: int = 0x300) -> RawFrameEnvelope:
    return RawFrameEnvelope(
        adapter_epoch=1,
        channel=ChannelId.HIGH,
        backend_arrival_ns=seq * 1_000_000,
        can_id=can_id,
        dlc=0,
        data=b"",
        channel_sequence=seq,
        global_sequence=seq,
    )


def test_history_capacity_and_drop_count():
    h = FrameHistory(capacity=3)
    for i in range(5):
        h.append(_env(i))
    metrics = h.metrics()
    assert metrics["size"] == 3
    assert metrics["dropped"] == 2
    assert metrics["total_appended"] == 5
    snap = h.snapshot()
    assert [f.channel_sequence for f in snap] == [2, 3, 4]


def test_history_limit_slice():
    h = FrameHistory(capacity=10)
    for i in range(5):
        h.append(_env(i))
    assert len(h.snapshot(limit=2)) == 2
