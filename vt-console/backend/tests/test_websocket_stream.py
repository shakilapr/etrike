"""WebSocket stream: coalesced state, heartbeat, events, resync (workplan §1.6)."""

from __future__ import annotations

from vtc import protocol_bridge as proto
from vtc.models.frames import ChannelId


def _read_until(ws, predicate, limit=200):
    """Read frames until one satisfies predicate; returns it (or asserts)."""
    for _ in range(limit):
        msg = ws.receive_json()
        if predicate(msg):
            return msg
    raise AssertionError("expected message not received within limit")


def test_hello_and_initial_state(client):
    with client.websocket_connect("/api/v1/stream") as ws:
        hello = ws.receive_json()
        assert hello["type"] == "hello"
        assert hello["wire_hash"] == proto.WIRE_HASH
        state = ws.receive_json()
        assert state["type"] == "state"
        assert state["batch_seq"] == 1
        assert state["messages"] == []  # nothing injected yet


def test_injection_produces_a_coalesced_state_batch(client):
    transport = client.app.state.lifecycle.transport
    with client.websocket_connect("/api/v1/stream") as ws:
        ws.receive_json()  # hello
        ws.receive_json()  # initial state

        transport.inject(ChannelId.LOW, 0x7FE, bytes.fromhex("ffff"))
        state = _read_until(
            ws,
            lambda m: m["type"] == "state"
            and any(x["name"] == "SYS_HEARTBEAT" for x in m["messages"]),
        )
        hb = next(x for x in state["messages"] if x["name"] == "SYS_HEARTBEAT")
        assert hb["freshness"] == "live"
        assert hb["signals"]["alive_ctr"]["engineering_value"] == 255


def test_heartbeat_when_idle(client):
    with client.websocket_connect("/api/v1/stream") as ws:
        ws.receive_json()  # hello
        ws.receive_json()  # initial state
        # Empty store: no version change -> a heartbeat follows within ~250ms.
        hb = _read_until(ws, lambda m: m["type"] == "heartbeat")
        assert "server_time_ns" in hb


def test_batch_sequence_is_monotonic(client):
    with client.websocket_connect("/api/v1/stream") as ws:
        ws.receive_json()  # hello (no batch_seq)
        seqs = [ws.receive_json()["batch_seq"] for _ in range(5)]
        assert seqs == sorted(seqs)
        assert len(set(seqs)) == len(seqs)  # strictly increasing


def test_resync_forces_a_fresh_state_batch(client):
    with client.websocket_connect("/api/v1/stream") as ws:
        ws.receive_json()  # hello
        ws.receive_json()  # initial state
        ws.send_json({"type": "resync"})
        forced = _read_until(ws, lambda m: m["type"] == "state")
        assert forced["type"] == "state"


def test_critical_event_is_delivered(client):
    bus = client.app.state.lifecycle.event_bus
    with client.websocket_connect("/api/v1/stream") as ws:
        ws.receive_json()  # hello
        ws.receive_json()  # initial state
        bus.publish({"code": "test.event", "severity": "info"})
        ev = _read_until(ws, lambda m: m["type"] == "event")
        assert ev["event"]["code"] == "test.event"
