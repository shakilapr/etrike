"""Vehicle-mode / motion-gate derivation, including NODE_STATUS authority."""

from __future__ import annotations

from control_toolkit.models.state import FreshnessState, MessageState, SignalValue
from control_toolkit.services.mode_gate import derive_motion_gate, derive_vehicle_mode


def _node_status(state: int) -> MessageState:
    return MessageState(
        bus="low",
        can_id=0x501,
        name="RT_NODE_STATUS",
        freshness=FreshnessState.LIVE,
        signals={"node_state": SignalValue(engineering_value=state, valid=True)},
    )


def test_derive_vehicle_mode_rt_inhibited_overrides_auto():
    info = derive_vehicle_mode([_node_status(4)])  # INHIBITED
    assert info["mode"] == "INHIBITED"
    assert info["source"] == "rt_node"


def test_derive_vehicle_mode_rt_estop_overrides_auto():
    info = derive_vehicle_mode([_node_status(5)])  # ESTOP
    assert info["mode"] == "ESTOP"


def test_motion_gated_when_node_inhibited():
    control = {
        "active": True,
        "method": "high_kinematics",
        "shaped_speed_mmps": 1000,
        "hard_brake": False,
    }
    gate = derive_motion_gate(control, [_node_status(4)])
    assert gate["gated"] is True
    assert "INHIBITED" in (gate["reason"] or "")
    assert gate["vehicle_mode"] == "INHIBITED"
