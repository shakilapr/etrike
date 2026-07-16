"""Smoke tests for the Phase 1 API surface."""

from __future__ import annotations

from vtc import protocol_bridge as proto


def test_status_ok(client):
    r = client.get("/api/v1/status")
    assert r.status_code == 200
    body = r.json()
    assert body["ready"] is True
    assert body["wire_hash"] == proto.WIRE_HASH
    assert body["catalog"] == {"messages": 32, "instances": 42}
    assert body["profile"] == "pure_software"


def test_state_snapshot_is_valid_and_empty(client):
    r = client.get("/api/v1/state")
    assert r.status_code == 200
    body = r.json()
    assert body["wire_hash"] == proto.WIRE_HASH
    assert body["messages"] == []
    assert body["sequence"] >= 1


def test_protocol_messages_list(client):
    r = client.get("/api/v1/protocol/messages")
    assert r.status_code == 200
    assert r.json()["count"] == 32


def test_protocol_message_detail_hex_and_404(client):
    ok = client.get("/api/v1/protocol/messages/low/0x204")
    assert ok.status_code == 200
    assert ok.json()["name"] == "RT_DRIVE_CMD"

    # SYS heartbeat is Low-only -> High lookup is a 404.
    missing = client.get("/api/v1/protocol/messages/high/0x7FE")
    assert missing.status_code == 404


def test_stream_hello_then_initial_state(client):
    with client.websocket_connect("/api/v1/stream") as ws:
        hello = ws.receive_json()
        assert hello["type"] == "hello"
        assert hello["wire_hash"] == proto.WIRE_HASH
        assert "server_time_ns" in hello

        first = ws.receive_json()
        assert first["type"] == "state"
        assert first["batch_seq"] == 1
        assert "messages" in first
