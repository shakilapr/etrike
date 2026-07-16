"""GET /api/v1/state, /history, /topology."""

from __future__ import annotations

import time

from control_toolkit.models.frames import ChannelId


def test_state_snapshot_schema(client):
    r = client.get("/api/v1/state")
    assert r.status_code == 200
    body = r.json()
    assert "sequence" in body
    assert "wire_hash" in body
    assert isinstance(body["messages"], list)


def test_history_empty_metrics(client):
    r = client.get("/api/v1/history")
    assert r.status_code == 200
    body = r.json()
    assert body["metrics"]["capacity"] >= 1
    assert "frames" in body


def test_topology_lists_expected_nodes(client):
    r = client.get("/api/v1/topology")
    assert r.status_code == 200
    nodes = {n["node"] for n in r.json()["nodes"]}
    assert {"Host", "RT_high", "RT_low", "SYS", "MTR"} <= nodes


def test_state_after_inject_has_expected_rate(client):
    transport = client.app.state.lifecycle.transport
    transport.inject(ChannelId.LOW, 0x7FE, bytes.fromhex("0100"))
    time.sleep(0.05)
    transport.inject(ChannelId.LOW, 0x7FE, bytes.fromhex("0200"))
    deadline = time.monotonic() + 3.0
    while time.monotonic() < deadline:
        msgs = client.get("/api/v1/state").json()["messages"]
        hb = next((m for m in msgs if m["name"] == "SYS_HEARTBEAT"), None)
        if hb and hb.get("observed_rate_hz"):
            break
        time.sleep(0.02)
    assert hb is not None
    assert hb["expected_rate_hz"] == 10.0  # 100 ms cycle
