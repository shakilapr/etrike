"""Encode engineering values via the shared protocol codecs."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from control_toolkit import protocol_bridge as proto
from control_toolkit.protocol_bridge import Frame


@dataclass(frozen=True, slots=True)
class EncodeResult:
    ok: bool
    status: str
    bus: str
    can_id: int
    key: str
    name: str
    dlc: int
    data: bytes
    is_extended: bool
    signals: dict[str, Any]
    warnings: list[str]


def encode_message(
    *,
    key: str | None = None,
    bus: str,
    can_id: int | None = None,
    values: dict[str, Any] | None = None,
) -> EncodeResult:
    """Encode a message by catalog key or (bus, can_id)."""
    values = dict(values or {})
    warnings: list[str] = []

    if key is None:
        if can_id is None:
            return _fail("missing_identity", bus, 0, "", "", values, warnings)
        key = proto.message_key_for(bus, can_id)
        if key is None:
            return _fail("unknown_id", bus, can_id, None, "UNKNOWN", values, warnings)

    meta = proto.CATALOG.get(key)
    if meta is None:
        return _fail("unknown_key", bus, can_id or 0, key, key, values, warnings)

    inst = None
    for item in meta.get("instances", []):
        if item["bus"] == bus and (can_id is None or int(item["id"]) == int(can_id)):
            inst = item
            break
    if inst is None and can_id is None:
        # Prefer the requested bus instance if unique on that bus.
        matches = [i for i in meta["instances"] if i["bus"] == bus]
        if len(matches) == 1:
            inst = matches[0]
    if inst is None:
        return _fail(
            "wrong_bus",
            bus,
            can_id or 0,
            key,
            meta["name"],
            values,
            warnings,
        )

    resolved_id = int(inst["id"])
    status, frame = proto.encode(key, values, bus=bus)
    if status != "ok" or frame is None:
        return EncodeResult(
            ok=False,
            status=status,
            bus=bus,
            can_id=resolved_id,
            key=key,
            name=meta["name"],
            dlc=int(meta["dlc"]),
            data=b"",
            is_extended=inst.get("frame_format") == "extended",
            signals=values,
            warnings=warnings,
        )

    # Round-trip self-check for positive encodes.
    check_status, decoded = proto.decode(key, frame)
    if check_status != "ok":
        warnings.append(f"roundtrip_decode:{check_status}")

    return EncodeResult(
        ok=True,
        status="ok",
        bus=bus,
        can_id=resolved_id,
        key=key,
        name=meta["name"],
        dlc=len(frame.data),
        data=bytes(frame.data),
        is_extended=frame.frame_format == "extended",
        signals=decoded if isinstance(decoded, dict) else values,
        warnings=warnings,
    )


def _fail(
    status: str,
    bus: str,
    can_id: int,
    key: str | None,
    name: str,
    values: dict[str, Any],
    warnings: list[str],
) -> EncodeResult:
    return EncodeResult(
        ok=False,
        status=status,
        bus=bus,
        can_id=can_id,
        key=key or "",
        name=name,
        dlc=0,
        data=b"",
        is_extended=False,
        signals=values,
        warnings=warnings,
    )
