"""Smoke tests for the HTTP/WebSocket API surface."""

from __future__ import annotations

from control_toolkit import protocol_bridge as proto


def test_status_ok(client):
    r = client.get("/api/v1/status")
    assert r.status_code == 200
    body = r.json()
    assert body["ready"] is True
    assert body["wire_hash"] == proto.WIRE_HASH
    assert body["catalog"] == {"messages": 32, "instances": 42}
    # Top-level profile follows session (default pure_software with no session).
    assert body["profile"] == "pure_software"
    assert body["default_profile"] == "pure_software"


def test_status_profile_tracks_session(client):
    """Top-level status.profile follows the session profile, not only config default."""
    from control_toolkit.config import Profile

    created = client.post("/api/v1/sessions", json={"profile": "pure_software"})
    assert created.status_code == 200
    body = client.get("/api/v1/status").json()
    assert body["profile"] == body["session"]["profile"] == "pure_software"
    assert body["default_profile"] == "pure_software"

    # Force a physical profile on the session manager without opening CANalyst
    # (avoids USB contention with a live toolkit process and message pollution).
    life = client.app.state.lifecycle
    with life.sessions._lock:
        life.sessions._state.profile = Profile.BENCH_TEST
        life.sessions._state.destination = "physical"
    body2 = client.get("/api/v1/status").json()
    assert body2["session"]["profile"] == "bench_test"
    assert body2["profile"] == "bench_test"
    assert body2["default_profile"] == "pure_software"


def test_state_snapshot_is_valid(client):
    r = client.get("/api/v1/state")
    assert r.status_code == 200
    body = r.json()
    assert body["wire_hash"] == proto.WIRE_HASH
    # Startup may auto-open Pure Software virtual buses + managed SYS peer,
    # so messages can already be non-empty. Require a valid list only.
    assert isinstance(body["messages"], list)
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


def test_stream_handshake_and_initial_state(client):
    with client.websocket_connect("/api/v1/stream") as ws:
        hello = ws.receive_json()
        assert hello["type"] == "hello"
        assert hello["wire_hash"] == proto.WIRE_HASH

        initial = ws.receive_json()
        assert initial["type"] == "state"
        assert initial.get("initial") is True
        assert initial["wire_hash"] == proto.WIRE_HASH
        assert isinstance(initial["messages"], list)

        ws.send_text("ping")
        # May interleave with broadcast state/heartbeat; wait for ack.
        for _ in range(20):
            msg = ws.receive_json()
            if msg.get("type") == "ack":
                assert msg == {"type": "ack", "echo": "ping"}
                break
        else:
            raise AssertionError("no ack received")
