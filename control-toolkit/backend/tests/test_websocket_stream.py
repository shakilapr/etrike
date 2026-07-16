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
