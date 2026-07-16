"""Virtual bus end-to-end pipeline (workplan §1 integration).

Alias-style suite covering inject → decode → state/history/topology APIs.
"""

from __future__ import annotations

import time

from control_toolkit.models.frames import ChannelId


def _wait_messages(client, min_count: int = 1, timeout: float = 5.0):
    deadline = time.monotonic() + timeout
    body = {"messages": []}
    while time.monotonic() < deadline:
        body = client.get("/api/v1/state").json()
        if len(body["messages"]) >= min_count:
            return body
        time.sleep(0.02)
    return body


def test_virtual_pipeline_inject_to_state(client):
    transport = client.app.state.lifecycle.transport
    assert transport is not None
    # SYS heartbeat golden: alive=255 all flags
    transport.inject(ChannelId.LOW, 0x7FE, bytes.fromhex("ffff"))
    body = _wait_messages(client)
    msgs = {m["name"]: m for m in body["messages"]}
    assert "SYS_HEARTBEAT" in msgs
    assert msgs["SYS_HEARTBEAT"]["freshness"] == "live"
    assert msgs["SYS_HEARTBEAT"]["validation_status"] == "ok"
    assert msgs["SYS_HEARTBEAT"]["signals"]["alive_ctr"]["engineering_value"] == 255
    assert msgs["SYS_HEARTBEAT"]["signals"]["alive_ctr"]["raw_value"] == 255


def test_virtual_pipeline_unknown_frame_in_history(client):
    transport = client.app.state.lifecycle.transport
    transport.inject(ChannelId.HIGH, 0x555, b"\x01\x02")
    deadline = time.monotonic() + 3.0
    while time.monotonic() < deadline:
        hist = client.get("/api/v1/history?limit=50").json()
        if any(f["can_id"] == 0x555 for f in hist["frames"]):
            break
        time.sleep(0.02)
    else:
        raise AssertionError("unknown frame never entered history")
    frame = next(f for f in hist["frames"] if f["can_id"] == 0x555)
    assert frame["data_hex"] == "0102"
    assert frame["dlc"] == 2

    state = client.get("/api/v1/state").json()
    unk = next(m for m in state["messages"] if m["can_id"] == 0x555)
    assert unk["name"] == "UNKNOWN"
    assert unk["validation_status"] == "unknown_id"
    assert unk["signals"] == {}


def test_virtual_pipeline_topology_updates(client):
    transport = client.app.state.lifecycle.transport
    transport.inject(ChannelId.LOW, 0x7FE, bytes.fromhex("0a01"))
    _wait_messages(client)
    deadline = time.monotonic() + 2.0
    while time.monotonic() < deadline:
        topo = client.get("/api/v1/topology").json()
        sys_node = next(n for n in topo["nodes"] if n["node"] == "SYS")
        if sys_node["liveness"] == "live":
            break
        time.sleep(0.05)
    else:
        raise AssertionError("SYS never became live in topology")
