"""Bit-grid layout from protocol catalog."""

from __future__ import annotations

from control_toolkit import protocol_bridge as proto
from control_toolkit.services.bit_layout import build_bit_grid, field_cells


def test_field_cells_linear():
    cells = field_cells(0, 0, 16)
    assert len(cells) == 16
    assert cells[0] == {"byte": 0, "bit": 0, "abs_bit": 0, "field_bit": 0}
    assert cells[8]["byte"] == 1
    assert cells[8]["bit"] == 0


def test_host_drive_grid_occupies_all_bytes():
    msg = proto.CATALOG["host:host_drive_cmd"]
    grid = build_bit_grid(msg)
    assert grid["dlc"] == 8
    assert "Motorola" in grid["endian_label"] or "Intel" in grid["endian_label"]
    keys = {f["key"] for f in grid["fields"]}
    assert "speed_mmps" in keys
    assert "gear" in keys
    # speed 32 bits at byte 0 → all bits of bytes 0-3 owned
    for by in range(4):
        for cell in grid["rows"][by]["bits"]:
            assert cell["field"] == "speed_mmps"


def test_protocol_layout_endpoint(client):
    r = client.get("/api/v1/protocol/messages/high/0x300/layout")
    assert r.status_code == 200, r.text
    body = r.json()
    assert body["name"] == "HOST_DRIVE_CMD"
    assert body["bit_grid"]["dlc"] == 8
    assert len(body["bit_grid"]["fields"]) >= 3
    detail = client.get("/api/v1/protocol/messages/high/0x300")
    assert "bit_grid" in detail.json()
