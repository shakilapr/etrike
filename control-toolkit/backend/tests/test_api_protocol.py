"""GET /api/v1/protocol/* catalog API."""

from control_toolkit import protocol_bridge as proto


def test_list_messages(client):
    r = client.get("/api/v1/protocol/messages")
    assert r.status_code == 200
    body = r.json()
    assert body["count"] == 34
    assert body["wire_hash"] == proto.WIRE_HASH
    assert body["semantic_hash"] == proto.SEMANTIC_HASH
    assert body["network_hash"] == proto.NETWORK_HASH
    assert len(body["instances"]) == 44


def test_get_message_by_hex_id(client):
    r = client.get("/api/v1/protocol/messages/high/0x300")
    assert r.status_code == 200
    assert r.json()["name"] == "HOST_DRIVE_CMD"
    assert r.json()["key"] == "host:host_drive_cmd"


def test_get_message_404(client):
    r = client.get("/api/v1/protocol/messages/high/0x7FE")
    assert r.status_code == 404
