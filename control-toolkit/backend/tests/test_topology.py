"""Topology / ECU liveness tracker."""

import time

from control_toolkit.models.state import FreshnessState, MessageState
from control_toolkit.state.topology import NodeLiveness, TopologyTracker


def test_observe_sys_heartbeat_live():
    t = TopologyTracker()
    t.observe(
        MessageState(
            bus="low",
            can_id=0x7FE,
            name="SYS_HEARTBEAT",
            last_seen_ns=time.monotonic_ns(),
            freshness=FreshnessState.LIVE,
            validation_status="ok",
            expected_rate_hz=10.0,
        )
    )
    snap = t.snapshot()
    sys_node = next(n for n in snap.nodes if n.node == "SYS")
    assert sys_node.liveness == NodeLiveness.LIVE


def test_rt_high_and_low_are_independent():
    t = TopologyTracker()
    t.observe(
        MessageState(
            bus="high",
            can_id=0x7FD,
            name="RT_HEARTBEAT",
            last_seen_ns=time.monotonic_ns(),
            freshness=FreshnessState.LIVE,
            validation_status="ok",
        )
    )
    snap = t.snapshot()
    by = {n.node: n for n in snap.nodes}
    assert by["RT_high"].liveness == NodeLiveness.LIVE
    assert by["RT_low"].liveness == NodeLiveness.OFFLINE
