from __future__ import annotations


def test_decode_known_history_frame(client) -> None:
    response = client.post(
        "/api/v1/protocol/decode",
        json={"bus": "high", "can_id": 0x300, "data_hex": "0000000000000000"},
    )
    assert response.status_code == 200
    body = response.json()
    assert body["known"] is True
    assert body["status"] == "ok"
    assert body["key"] == "host:host_drive_cmd"
    assert body["signals"] is not None


def test_decode_rejects_invalid_hex_and_identifies_unknown_frames(client) -> None:
    invalid = client.post(
        "/api/v1/protocol/decode",
        json={"bus": "high", "can_id": 0x300, "data_hex": "not-hex"},
    )
    assert invalid.status_code == 400
    assert invalid.json()["code"] == "protocol.invalid_data_hex"

    unknown = client.post(
        "/api/v1/protocol/decode",
        json={"bus": "high", "can_id": 0x123, "data_hex": "00"},
    )
    assert unknown.status_code == 200
    assert unknown.json()["known"] is False
