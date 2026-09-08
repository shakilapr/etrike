"""Encode engineering values via the shared protocol codecs."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from control_toolkit import protocol_bridge as proto

from protocol.e2e import crc8_h2f  # type: ignore[import-not-found]

#: AUTOSAR E2E Data-IDs for frames carrying an ``e2e_crc`` layout field.
#: ``sys_safety_sts`` is the canonical protected frame (Data-ID 0x3C11, CRC over
#: bytes [0..3]). NODE_STATUS frames carry a reserved ``e2e_crc`` byte with no
#: contract yet; we fold Data-ID 0 so all three nodes agree on identical payloads.
_E2E_DATA_IDS: dict[str, int] = {
    "sys:sys_safety_sts": 0x3C11,
    "sys:sys_node_status": 0,
    "rt:rt_node_status": 0,
    "mtr:mtr_node_status": 0,
}

#: Per-message wrapping rolling-counter state (mod-256, increment 1 per emission).
_counter_state: dict[str, int] = {}


def _next_counter(key: str) -> int:
    value = (_counter_state.get(key, -1) + 1) & 0xFF
    _counter_state[key] = value
    return value


def _fill_missing_counter(key: str, meta: dict[str, Any], values: dict[str, Any]) -> None:
    if "rolling_counter" in values:
        return
    fields = meta.get("layout", {}).get("fields", [])
    if not any(f.get("key") == "rolling_counter" for f in fields):
        return
    values["rolling_counter"] = _next_counter(key)


def _apply_e2e(key: str, meta: dict[str, Any], bus: str, values: dict[str, Any]) -> tuple[str, bytes | None]:
    """Return ``("ok", frame)`` for a frame that carries no protected E2E
    (plain encode), or run the two-pass AUTOSAR CRC: encode with a zero
    placeholder, compute the CRC over the protected bytes, re-encode with the
    real value."""
    fields = meta.get("layout", {}).get("fields", [])
    if not any(f.get("key") == "e2e_crc" for f in fields):
        return proto.encode(key, values, bus=bus)
    data_id = _E2E_DATA_IDS.get(key)
    if data_id is None:
        return proto.encode(key, values, bus=bus)
    values = dict(values)
    values["e2e_crc"] = 0
    status, frame = proto.encode(key, values, bus=bus)
    if status != "ok" or frame is None or len(frame.data) < 1:
        return status, None
    crc = crc8_h2f(frame.data[: len(frame.data) - 1], data_id)
    values["e2e_crc"] = crc
    return proto.encode(key, values, bus=bus)


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
    auto_counter: bool = False,
    auto_e2e: bool = False,
) -> EncodeResult:
    """Encode a message by catalog key or (bus, can_id).

    ``auto_counter`` fills a missing ``rolling_counter`` field (protocol wrapping
    counter, mod 256, +1 per emission per message). ``auto_e2e`` computes the
    AUTOSAR-profile ``e2e_crc`` for frames that carry one (two-pass encode).
    Both only apply when the caller omits the field.
    """
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

    if auto_counter:
        _fill_missing_counter(key, meta, values)
    if auto_e2e:
        status, e2e_frame = _apply_e2e(key, meta, bus, values)
        if status != "ok" or e2e_frame is None:
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
        frame = e2e_frame
    else:
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
