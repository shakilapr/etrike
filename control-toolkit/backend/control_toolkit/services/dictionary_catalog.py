"""CAN Dictionary catalog built from YAML-generated protocol metadata.

Shape mirrors debug-tool MessageCard / BitGrid / SignalTable consumers:
one entry per physical bus instance, fields with byte/bit/size layout.

Generated ``layout.fields`` are preferred. Vendor opaque SES/SEB frames have no
generated fields — those use :mod:`vendor_field_layouts` keyed to custom codec
signal names so the dictionary is complete for 0x201 SES_STATUS and peers.
"""

from __future__ import annotations

from typing import Any

from control_toolkit import protocol_bridge as proto
from control_toolkit.services.vendor_field_layouts import resolve_layout_fields


def _hex_id(can_id: int | str) -> str:
    if isinstance(can_id, str):
        try:
            can_id = int(can_id, 0)
        except ValueError:
            return can_id
    return f"0x{int(can_id):X}"


def _field_def(field: dict[str, Any]) -> dict[str, Any]:
    key = str(field.get("key") or field.get("name") or "?")
    bits = int(field.get("bits") or field.get("size") or 0)
    signed = bool(field.get("signed"))
    factor = float(field.get("factor") if field.get("factor") is not None else 1)
    offset = float(field.get("offset") if field.get("offset") is not None else 0)
    enum = field.get("enum") or {}
    options = None
    if isinstance(enum, dict) and enum:
        options = [
            {
                "value": int(k) if str(k).lstrip("-").isdigit() else k,
                "label": str(v),
            }
            for k, v in enum.items()
        ]
        try:
            options.sort(key=lambda o: int(o["value"]))
        except (TypeError, ValueError):
            pass

    kind = "enum" if options else "number"
    if bits == 1 and not options:
        kind = "boolean"

    return {
        "key": key,
        "label": key,
        "kind": kind,
        "unit": field.get("unit") or "",
        "min": field.get("min"),
        "max": field.get("max"),
        "options": options,
        "comment": field.get("comment") or "",
        "_byte": int(field.get("byte") or 0),
        "_bit_offset": int(field.get("bit") or field.get("bit_offset") or 0),
        "_size": bits,
        "_type": "signed" if signed else "unsigned",
        "_factor": factor,
        "_offset": offset,
    }


# Custom codecs that still implement encode() (command TX). Status/telemetry
# frames are RX-only in the protocol package until encoders are added.
_ENCODEABLE_CUSTOM_KEYS = frozenset(
    {
        "ses:vcu_ses_req",
        "seb:vcu_seb_req",
    }
)


def _can_named_inject(key: str, strategy: str, dlc: int, field_count: int) -> bool:
    """Whether named inject can build a wire payload for this catalog key."""
    if strategy == "generated":
        return True
    if key in _ENCODEABLE_CUSTOM_KEYS:
        return True
    # Empty-payload frames (e.g. SAFETY_ESTOP) need no field encode.
    if dlc == 0 or field_count == 0:
        return True
    return False


def build_dictionary_messages() -> list[dict[str, Any]]:
    """Expand catalog messages x instances into dictionary rows (high/low only)."""
    out: list[dict[str, Any]] = []
    for key, msg in proto.CATALOG.items():
        layout = msg.get("layout") or {}
        raw_fields = resolve_layout_fields(key, layout)
        fields = [_field_def(f) for f in raw_fields]
        vendor_fill = not (layout.get("fields") or []) and bool(fields)
        comment = layout.get("algorithm") or msg.get("comment") or ""
        codec = msg.get("codec") or {}
        strategy = codec.get("strategy") or "generated"
        byte_order = msg.get("byte_order") or "big"
        endian = "motorola" if byte_order in ("big", "motorola") else "intel"
        dlc = int(msg.get("dlc") or 0)
        name = str(msg.get("name") or key)
        inject_ok = _can_named_inject(key, strategy, dlc, len(fields))

        for inst in msg.get("instances") or []:
            bus = str(inst.get("bus") or "")
            if bus not in ("high", "low"):
                continue
            can_id = inst.get("id")
            cycle = int(inst.get("cycle_ms") or 0)
            period = "event" if cycle == 0 else f"{cycle}ms"
            receivers = list(inst.get("receivers") or [])
            cid_int = (
                int(can_id, 0) if isinstance(can_id, str) else int(can_id or 0)
            )
            out.append(
                {
                    "bus": bus,
                    "id": _hex_id(can_id if can_id is not None else 0),
                    "can_id": cid_int,
                    "name": name,
                    "sender": str(inst.get("sender") or "—"),
                    "dlc": dlc,
                    "period": period,
                    "cycle_ms": cycle,
                    "receivers": receivers,
                    "comment": comment,
                    "byteOrder": endian,
                    "byte_order": byte_order,
                    "fields": fields,
                    "canonicalKey": key,
                    "frameFormat": inst.get("frame_format") or "standard",
                    "layout_kind": layout.get("kind") or "signals",
                    "source": "vendor_codec_map" if vendor_fill else "yaml",
                    "capabilities": {
                        "rawMonitoring": True,
                        "semanticDecode": strategy == "generated"
                        or strategy == "custom"
                        or bool(fields),
                        # Named inject when encode path exists (generated + known custom cmds).
                        "decodedInjection": inject_ok,
                        "codecStrategy": strategy,
                        "implementation": codec.get("implementation_id"),
                    },
                }
            )

    out.sort(key=lambda m: (m["bus"], m["can_id"], m["name"]))
    return out


def dictionary_payload() -> dict[str, Any]:
    messages = build_dictionary_messages()
    signal_count = sum(len(m["fields"]) for m in messages)
    return {
        "wire_hash": proto.WIRE_HASH,
        "semantic_hash": proto.SEMANTIC_HASH,
        "network_hash": proto.NETWORK_HASH,
        "source": "protocol/contracts YAML → generated catalog (+ vendor field maps)",
        "count": len(messages),
        "signal_count": signal_count,
        "messages": messages,
    }
