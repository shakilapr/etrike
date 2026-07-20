"""Protocol catalog browse endpoints (workplan §1.6).

Serves the YAML-generated message catalog so the frontend renders identity,
layout, and the CAN Dictionary bit grid from the same source of truth the
firmware uses.
"""

from __future__ import annotations

import importlib
import logging

from fastapi import APIRouter, Request
from pydantic import BaseModel

from control_toolkit import protocol_bridge as proto
from control_toolkit.services.bit_layout import build_bit_grid
from control_toolkit.services.dictionary_catalog import dictionary_payload
from control_toolkit.services.session_manager import SessionError

log = logging.getLogger("control_toolkit.protocol")

router = APIRouter(prefix="/protocol", tags=["protocol"])


class DecodeFrameRequest(BaseModel):
    bus: str
    can_id: int
    data_hex: str
    is_extended: bool = False


@router.post("/decode")
def decode_frame(body: DecodeFrameRequest) -> dict:
    """Decode one evidence/history frame through the generated protocol codec."""
    key = proto.message_key_for(body.bus, body.can_id)
    if key is None:
        return {
            "known": False,
            "status": "unknown_id",
            "bus": body.bus,
            "can_id": body.can_id,
            "data_hex": body.data_hex,
            "signals": None,
        }
    try:
        data = bytes.fromhex(body.data_hex)
    except ValueError as exc:
        raise SessionError("protocol.invalid_data_hex", "data_hex is not valid hexadecimal", status=400) from exc
    frame = proto.Frame(
        bus=body.bus,
        id=body.can_id,
        frame_format="extended" if body.is_extended else "standard",
        data=data,
    )
    status, values = proto.decode(key, frame)
    return {
        "known": True,
        "status": str(status),
        "bus": body.bus,
        "can_id": body.can_id,
        "data_hex": body.data_hex,
        "key": key,
        "name": proto.CATALOG[key]["name"],
        "signals": values,
    }


@router.get("/messages")
def list_messages() -> dict:
    return {
        "wire_hash": proto.WIRE_HASH,
        "semantic_hash": proto.SEMANTIC_HASH,
        "network_hash": proto.NETWORK_HASH,
        "count": proto.message_count(),
        "instances": proto.instances(),
    }


@router.get("/dictionary")
def get_dictionary() -> dict:
    """Full CAN dictionary for UI (debug-tool MessageCard structure, YAML-sourced)."""
    return dictionary_payload()


@router.post("/dictionary/refresh")
def refresh_dictionary() -> dict:
    """Re-bind protocol package hashes/catalog after regenerate; clear path caches."""
    try:
        import protocol.generated.python.etrike_protocol as ep

        importlib.reload(ep)
        # Re-bind bridge module globals from reloaded package.
        proto.SEMANTIC_HASH = ep.SEMANTIC_HASH
        proto.WIRE_HASH = ep.WIRE_HASH
        proto.NETWORK_HASH = ep.NETWORK_HASH
        proto.CATALOG = ep.METADATA
        if hasattr(proto._bus_id_index, "cache_clear"):
            proto._bus_id_index.cache_clear()
        log.info(
            "protocol catalog reloaded wire=%s messages=%s",
            proto.WIRE_HASH[:12],
            len(proto.CATALOG),
        )
    except Exception as exc:  # noqa: BLE001
        log.warning("protocol reload partial: %s", exc)
        if hasattr(proto._bus_id_index, "cache_clear"):
            proto._bus_id_index.cache_clear()

    body = dictionary_payload()
    body["refreshed"] = True
    return body


def _resolve_message(bus: str, can_id: str) -> tuple[str, dict]:
    try:
        cid = int(can_id, 0)
    except ValueError as exc:
        raise SessionError(
            "protocol.invalid_can_id",
            f"invalid can_id: {can_id!r}",
            status=400,
        ) from exc

    key = proto.message_key_for(bus, cid)
    if key is None:
        raise SessionError(
            "protocol.message_not_found",
            f"no message at bus={bus} id={hex(cid)}",
            status=404,
        )
    return key, proto.CATALOG[key]


@router.get("/messages/{bus}/{can_id}")
def get_message(bus: str, can_id: str) -> dict:
    key, msg = _resolve_message(bus, can_id)
    return {
        "key": key,
        "wire_hash": proto.WIRE_HASH,
        "semantic_hash": proto.SEMANTIC_HASH,
        **msg,
        "bit_grid": build_bit_grid(msg, catalog_key=key),
    }


@router.get("/messages/{bus}/{can_id}/layout")
def get_message_layout(bus: str, can_id: str, request: Request) -> dict:
    """Bit occupancy grid + optional live signal overlay from latest state."""
    key, msg = _resolve_message(bus, can_id)
    try:
        cid = int(can_id, 0)
    except ValueError:
        cid = 0
    grid = build_bit_grid(msg, catalog_key=key)
    live = None
    life = request.app.state.lifecycle
    snap = life.latest.snapshot()
    for m in snap.messages:
        if m.bus == bus and int(m.can_id) == cid:
            live = {
                "freshness": getattr(m.freshness, "value", m.freshness),
                "validation_status": getattr(
                    m.validation_status, "value", m.validation_status
                ),
                "last_seen_ns": m.last_seen_ns,
                "signals": {
                    name: {
                        "engineering_value": s.engineering_value,
                        "enum_label": s.enum_label,
                        "raw_value": s.raw_value,
                        "unit": s.unit,
                        "valid": s.valid,
                    }
                    for name, s in (m.signals or {}).items()
                },
            }
            break
    return {
        "key": key,
        "name": msg.get("name"),
        "bus": bus,
        "can_id": cid,
        "wire_hash": proto.WIRE_HASH,
        "bit_grid": grid,
        "live": live,
    }
