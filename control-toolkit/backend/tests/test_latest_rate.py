import pytest
from control_toolkit.models.state import MessageState
from control_toolkit.state.latest import LatestStore


def test_observed_rate_burst_drain_immunity():
    store = LatestStore()
    bus = "high"
    can_id = 0x121
    expected_rate = 100.0  # 10ms expected

    # Simulate normal steady 100 Hz arrivals (10 ms gaps)
    t = 1_000_000_000
    for _ in range(10):
        st = MessageState(
            bus=bus,
            can_id=can_id,
            name="RT_MOTION_RPT",
            last_seen_ns=t,
            expected_rate_hz=expected_rate,
        )
        store.upsert(st)
        t += 10_000_000  # 10 ms

    snap = store.snapshot(now_ns=t)
    msg = snap.messages[0]
    assert msg.observed_rate_hz is not None
    assert 95.0 <= msg.observed_rate_hz <= 105.0

    # Now simulate a long 2-second pause, followed by a burst of 5 frames flushed 0.05 ms apart (USB buffer drain)
    t += 2_000_000_000  # 2s gap
    for _ in range(5):
        t += 50_000  # 0.05 ms (50 microseconds)
        st = MessageState(
            bus=bus,
            can_id=can_id,
            name="RT_MOTION_RPT",
            last_seen_ns=t,
            expected_rate_hz=expected_rate,
        )
        store.upsert(st)

    # The burst gaps (<1ms and <0.25*10ms = 2.5ms) must be discarded.
    # The observed rate must NOT be 20,000 Hz.
    snap = store.snapshot(now_ns=t)
    msg = snap.messages[0]
    assert msg.observed_rate_hz is not None
    assert msg.observed_rate_hz <= 5.0 * expected_rate
    assert 95.0 <= msg.observed_rate_hz <= 105.0
