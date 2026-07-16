"""Encoder unit tests."""

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
