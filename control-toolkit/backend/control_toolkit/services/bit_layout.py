"""Canonical bit occupancy grid from protocol YAML layout metadata.

Uses the same (byte, bit, bits) model as protocol export tools:
start_bit = byte * 8 + bit where bit=0 is the LSB of that byte.
Occupancy is consecutive absolute bits for grid visualization (Intel/Motorola
byte_order is labeled for display; packing follows generated field extents).
"""

from __future__ import annotations

from typing import Any

from control_toolkit.services.vendor_field_layouts import resolve_layout_fields


def field_cells(byte: int, bit: int, bits: int) -> list[dict[str, int]]:
    """Return ordered cells ``{byte, bit, abs_bit}`` for a field extent."""
    start = int(byte) * 8 + int(bit)
    cells: list[dict[str, int]] = []
    for i in range(int(bits)):
        abs_bit = start + i
        cells.append(
            {
                "byte": abs_bit // 8,
                "bit": abs_bit % 8,  # 0 = LSB within byte
                "abs_bit": abs_bit,
                "field_bit": i,
            }
        )
    return cells


def build_bit_grid(message: dict[str, Any], catalog_key: str | None = None) -> dict[str, Any]:
    """Build a DLC × 8 bit grid with field ownership for one catalog message.

    ``catalog_key`` (e.g. ``ses:ses_status``) enables vendor field maps when the
    YAML layout is opaque / field-less.
    """
    dlc = int(message.get("dlc") or 0)
    byte_order = str(message.get("byte_order") or "big")
    # Map catalog labels to Intel/Motorola for UI.
    endian_label = "Motorola (big-endian)" if byte_order in ("big", "motorola") else "Intel (little-endian)"

    layout = message.get("layout") or {}
    fields = layout.get("fields") or []
    if not fields and catalog_key:
        fields = resolve_layout_fields(catalog_key, layout)
    field_meta: list[dict[str, Any]] = []
    # grid[byte][bit] = field key or None
    ownership: list[list[str | None]] = [[None for _ in range(8)] for _ in range(max(dlc, 1))]

    for f in fields:
        key = str(f.get("key") or f.get("name") or "?")
        b0 = int(f.get("byte") or 0)
        bit0 = int(f.get("bit") or 0)
        nbits = int(f.get("bits") or f.get("size") or 0)
        cells = field_cells(b0, bit0, nbits)
        for c in cells:
            by = c["byte"]
            bi = c["bit"]
            while by >= len(ownership):
                ownership.append([None for _ in range(8)])
            ownership[by][bi] = key
        field_meta.append(
            {
                "key": key,
                "byte": b0,
                "bit": bit0,
                "bits": nbits,
                "signed": bool(f.get("signed")),
                "min": f.get("min"),
                "max": f.get("max"),
                "unit": f.get("unit"),
                "enum": f.get("enum"),
                "cells": cells,
            }
        )

    rows: list[dict[str, Any]] = []
    n_bytes = max(dlc, len(ownership))
    for by in range(n_bytes):
        # Display MSB (bit 7) on the left — common CAN analyzer convention.
        bits_msb_left = []
        for display_col, bi in enumerate(range(7, -1, -1)):
            owner = ownership[by][bi] if by < len(ownership) else None
            bits_msb_left.append(
                {
                    "bit": bi,
                    "display_col": display_col,
                    "field": owner,
                }
            )
        rows.append({"byte": by, "bits": bits_msb_left})

    return {
        "dlc": dlc,
        "byte_order": byte_order,
        "endian_label": endian_label,
        "fields": field_meta,
        "rows": rows,
        "legend": {f["key"]: f["key"] for f in field_meta},
    }
