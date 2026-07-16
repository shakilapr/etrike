"""Protocol catalog browse endpoints (workplan §1.6).

Serves the YAML-generated message catalog so the frontend renders identity,
layout, and the CAN Dictionary bit grid from the same source of truth the
firmware uses.
"""

from __future__ import annotations

from fastapi import APIRouter, HTTPException

from control_toolkit import protocol_bridge as proto

router = APIRouter(prefix="/protocol", tags=["protocol"])


@router.get("/messages")
def list_messages() -> dict:
    return {
        "wire_hash": proto.WIRE_HASH,
        "count": proto.message_count(),
        "instances": proto.instances(),
    }


@router.get("/messages/{bus}/{can_id}")
def get_message(bus: str, can_id: str) -> dict:
    # Accept hex ("0x210") or decimal ids.
    try:
        cid = int(can_id, 0)
    except ValueError:
        raise HTTPException(status_code=400, detail=f"invalid can_id: {can_id!r}")

    key = proto.message_key_for(bus, cid)
    if key is None:
        raise HTTPException(
            status_code=404, detail=f"no message at bus={bus} id={hex(cid)}"
        )
    return {"key": key, "wire_hash": proto.WIRE_HASH, **proto.CATALOG[key]}
