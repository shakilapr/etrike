"""The bridge to the generated protocol package is the backend's foundation."""

from __future__ import annotations

from control_toolkit import protocol_bridge as proto


def test_catalog_includes_phase2_messages():
    assert proto.message_count() == 34
    assert proto.instance_count() == 44


def test_wire_hash_is_the_phase0_hash():
    assert proto.WIRE_HASH == (
        "5bec9d1ef7a06a158d1d620c0da85c28ec741d082f553964494675620d89027c"
    )
    assert proto.SEMANTIC_HASH == proto.WIRE_HASH
    assert len(proto.NETWORK_HASH) == 64
    assert all(c in "0123456789abcdef" for c in proto.NETWORK_HASH)


def test_rt_sys_runtime_identities_resolve():
    # Compatibility-contract messages resolve at their corrected (bus, id).
    assert proto.message_key_for("high", 0x300) == "host:host_drive_cmd"
    assert proto.message_key_for("high", 0x303) == "host:host_steer_cmd"
    assert proto.message_key_for("high", 0x121) == "rt:rt_motion_rpt"
    assert proto.message_key_for("low", 0x204) == "rt:rt_drive_cmd"
    assert proto.message_key_for("high", 0x210) == "rt:rt_state_rpt"
    assert proto.message_key_for("low", 0x210) == "rt:rt_state_rpt"
    # SYS heartbeat is Low-only; nothing on High.
    assert proto.message_key_for("low", 0x7FE) == "sys:sys_heartbeat"
    assert proto.message_key_for("high", 0x7FE) is None
    # Diagnostics keep canonical names (not "RT_DIAG").
    assert proto.CATALOG[proto.message_key_for("high", 0x310)]["name"] == "STEER_DIAG"
    assert proto.CATALOG[proto.message_key_for("high", 0x311)]["name"] == "BRAKE_DIAG"


def test_rt_heartbeat_is_independent_on_both_buses():
    assert proto.message_key_for("high", 0x7FD) == "rt:rt_heartbeat"
    assert proto.message_key_for("low", 0x7FD) == "rt:rt_heartbeat"
    insts = proto.CATALOG["rt:rt_heartbeat"]["instances"]
    assert {i["bus"] for i in insts} == {"high", "low"}
    assert all(i["semantics"] == "independent" for i in insts)
