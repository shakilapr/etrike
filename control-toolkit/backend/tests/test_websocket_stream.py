"""WebSocket /api/v1/stream smoke tests."""

from __future__ import annotations

import time

from control_toolkit import protocol_bridge as proto
from control_toolkit.models.frames import ChannelId


def test_websocket_hello_and_state_broadcast(client):
    with client.websocket_connect("/api/v1/stream") as ws:
        hello = ws.receive_json()
        assert hello["type"] == "hello"
        assert hello["wire_hash"] == proto.WIRE_HASH

        initial = ws.receive_json()
        assert initial["type"] == "state"
        assert initial.get("initial") is True
        assert "messages" in initial

        # Inject and wait for a non-initial state batch that includes SYS HB.
        client.app.state.lifecycle.transport.inject(
            ChannelId.LOW, 0x7FE, bytes.fromhex("0301")
        )
        deadline = time.monotonic() + 5.0
        saw = False
        while time.monotonic() < deadline:
            msg = ws.receive_json()
            if msg.get("type") == "state" and not msg.get("initial"):
                names = {m.get("name") for m in msg.get("messages", [])}
                if "SYS_HEARTBEAT" in names:
                    saw = True
                    break
            if msg.get("type") == "heartbeat":
                continue
        assert saw, "never received state broadcast with SYS_HEARTBEAT"


def test_websocket_survives_rapid_state_and_client_pings(client):
    """Regression: concurrent receive must not kill the handler under load.

    High-rate EventBus publishes + client pings used to cancel/recreate
    ``ws.receive_text()`` and raise Concurrent call to receive().
    """
    with client.websocket_connect("/api/v1/stream") as ws:
        assert ws.receive_json()["type"] == "hello"
        assert ws.receive_json()["type"] == "state"

        life = client.app.state.lifecycle
        # Burst bus traffic so the stream task pumps many state events.
        for i in range(40):
            life.transport.inject(ChannelId.LOW, 0x7FE, bytes([i & 0xFF, 0x01]))
            life.transport.inject(ChannelId.HIGH, 0x7FC, bytes([i & 0xFF, 0x01]))
            if i % 5 == 0:
                ws.send_text(f"ping-{i}")
            time.sleep(0.01)

        deadline = time.monotonic() + 6.0
        saw_ack = False
        saw_state = False
        saw_heartbeat = False
        while time.monotonic() < deadline and not (saw_ack and saw_state):
            msg = ws.receive_json()
            t = msg.get("type")
            if t == "ack":
                saw_ack = True
            elif t == "state" and not msg.get("initial"):
                saw_state = True
            elif t == "heartbeat":
                saw_heartbeat = True
        assert saw_state, "stream died before delivering state under load"
        assert saw_ack, "client pings were not acked (handler likely crashed)"
        # Heartbeat is best-effort in the burst window; state+ack prove liveness.
        _ = saw_heartbeat
