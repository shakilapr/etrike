"""Encoder unit tests."""

from protocol.e2e import sys_safety_sts_crc

from control_toolkit.services.encoder import encode_message


def test_encode_sys_heartbeat():
    r = encode_message(
        key="sys:sys_heartbeat",
        bus="low",
        values={
            "alive_ctr": 3,
            "heartbeat_ok": 1,
            "estop_active": 0,
            "mode_auto": 0,
            "can_ok": 1,
            "task_safety_ok": 1,
            "task_brake_ok": 1,
            "task_dispatch_ok": 1,
            "task_can_tx_ok": 1,
        },
    )
    assert r.ok
    assert r.can_id == 0x7FE
    assert r.dlc == 2
    assert r.data[0] == 3


def test_encode_estop_dlc0():
    r = encode_message(key="safety:safety_estop", bus="low", values={})
    assert r.ok
    assert r.dlc == 0
    assert r.data == b""


def test_encode_out_of_range():
    r = encode_message(
        key="host:host_drive_cmd",
        bus="high",
        values={"speed_mmps": 999999, "yaw_rate_mrad_s": 0, "gear": 0},
    )
    assert not r.ok


def test_encode_sys_safety_sts_needs_counter_and_crc():
    r = encode_message(
        key="sys:sys_safety_sts",
        bus="low",
        values={"estop_active": 0, "heartbeat_ok": 1},
    )
    assert not r.ok
    assert r.status == "value_out_of_range"


_SYS_SAFETY_BASE = {
    "estop_active": 0,
    "heartbeat_ok": 1,
    "light_left": 0,
    "light_right": 0,
    "light_brake": 0,
    "light_head": 0,
}


def test_encode_sys_safety_sts_auto_e2e_and_counter():
    r1 = encode_message(
        key="sys:sys_safety_sts",
        bus="low",
        values=dict(_SYS_SAFETY_BASE),
        auto_counter=True,
        auto_e2e=True,
    )
    assert r1.ok, r1.status
    assert r1.dlc == 5
    assert sys_safety_sts_crc(r1.data) == r1.data[4]

    r2 = encode_message(
        key="sys:sys_safety_sts",
        bus="low",
        values=dict(_SYS_SAFETY_BASE),
        auto_counter=True,
        auto_e2e=True,
    )
    assert r2.ok
    # Counter advances +1 (mod 256) between emissions.
    assert r2.data[3] == (r1.data[3] + 1) & 0xFF


def test_encode_sys_safety_sts_auto_preserves_explicit_counter():
    r = encode_message(
        key="sys:sys_safety_sts",
        bus="low",
        values={**_SYS_SAFETY_BASE, "rolling_counter": 42},
        auto_counter=True,
        auto_e2e=True,
    )
    assert r.ok, r.status
    assert r.data[3] == 42
    assert sys_safety_sts_crc(r.data) == r.data[4]


_NODE_STATUS_BASE = {
    "node_state": 3,
    "block_mask": 291,
    "estop_active": 1,
    "ready": 1,
    "command_received": 0,
    "command_nonzero": 0,
    "output_enabled": 0,
    "estop_latched": 0,
    "recovery_pending": 0,
    "degraded": 1,
    "rolling_counter": 7,
}


def test_encode_node_status_auto_e2e():
    for key, can_id in (
        ("sys:sys_node_status", 0x500),
        ("rt:rt_node_status", 0x501),
        ("mtr:mtr_node_status", 0x502),
    ):
        r = encode_message(key=key, bus="low", values=dict(_NODE_STATUS_BASE), auto_e2e=True)
        assert r.ok, (key, r.status)
        assert r.can_id == can_id
        assert r.dlc == 8
