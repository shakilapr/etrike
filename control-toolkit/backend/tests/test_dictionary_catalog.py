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


def test_dictionary_ses_status_has_vendor_fields():
    """Opaque SES_STATUS must expose codec-aligned signals (not empty)."""
    msgs = build_dictionary_messages()
    ses = [m for m in msgs if m["name"] == "SES_STATUS" and m["can_id"] == 0x201]
    assert ses, "SES_STATUS 0x201 missing from dictionary"
    m = ses[0]
    assert m["bus"] == "low"
    assert m["dlc"] == 8
    assert m["source"] == "vendor_codec_map"
    keys = {f["key"] for f in m["fields"]}
    for need in (
        "angle_aligned",
        "control_mode",
        "error_status",
        "steering_angle_raw",
        "target_angle_speed_raw",
        "steering_torque_raw",
        "rolling_counter_enabled",
        "checksum_enabled",
        "rolling_counter",
        "checksum",
    ):
        assert need in keys, f"missing {need} in SES_STATUS fields"
    angle = next(f for f in m["fields"] if f["key"] == "steering_angle_raw")
    assert angle["_byte"] == 2
    assert angle["_size"] == 16
    assert angle["_factor"] == 0.1
    assert angle["_offset"] == -3000
    err = next(f for f in m["fields"] if f["key"] == "error_status")
    assert err["options"]
    labels = {o["label"] for o in err["options"]}
    assert "Normal" in labels


def test_dictionary_vcu_ses_req_and_seb_status_populated():
    msgs = build_dictionary_messages()
    cmd = next(m for m in msgs if m["name"] == "VCU_SES_REQ")
    assert {f["key"] for f in cmd["fields"]} >= {
        "alignment_enable",
        "control_enable",
        "target_angle_raw",
        "rolling_counter",
    }
    seb = next(m for m in msgs if m["name"] == "SEB_STATUS")
    assert {f["key"] for f in seb["fields"]} >= {
        "alignment_status",
        "stroke_value_raw",
        "pressure_value_raw",
        "error_status",
    }


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
