"""YAML-sourced CAN dictionary catalog (debug-tool structure)."""

from __future__ import annotations

from control_toolkit.services.dictionary_catalog import (
    build_dictionary_messages,
    dictionary_payload,
)


def test_dictionary_has_host_drive_high():
    msgs = build_dictionary_messages()
    host = [m for m in msgs if m["name"] == "HOST_DRIVE_CMD" and m["bus"] == "high"]
    assert host
    m = host[0]
    assert m["id"] in ("0x300", "0x300")
    assert m["source"] == "yaml"
    assert m["sender"]
    assert m["byteOrder"] in ("motorola", "intel")
    keys = {f["key"] for f in m["fields"]}
    assert "speed_mmps" in keys
    assert "gear" in keys
    gear = next(f for f in m["fields"] if f["key"] == "gear")
    assert gear["_byte"] == 7
    assert gear["options"]


def test_dictionary_payload_and_endpoint(client):
    body = dictionary_payload()
    assert body["count"] >= 20
    assert body["signal_count"] >= 1
    assert body["wire_hash"]

    r = client.get("/api/v1/protocol/dictionary")
    assert r.status_code == 200
    data = r.json()
    assert data["count"] == body["count"]
    assert len(data["messages"]) == data["count"]

    ref = client.post("/api/v1/protocol/dictionary/refresh")
    assert ref.status_code == 200
    assert ref.json()["refreshed"] is True
    assert ref.json()["count"] >= 20
