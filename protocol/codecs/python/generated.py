from __future__ import annotations

import math
from collections.abc import Mapping, MutableMapping
from typing import Any

from protocol.generated.python.etrike_protocol import METADATA

from .types import CodecStatus, Frame, FrameFormat, validate_frame


def is_generated(message: str) -> bool:
    metadata = METADATA.get(message)
    return metadata is not None and metadata["codec"]["strategy"] == "generated"


def _message(message: str) -> dict[str, Any]:
    metadata = METADATA.get(message)
    if metadata is None:
        raise KeyError(message)
    if metadata["codec"]["strategy"] != "generated":
        raise ValueError(f"{message} selects a custom codec")
    return metadata


def _instance(metadata: Mapping[str, Any], bus: str) -> Mapping[str, Any] | None:
    matches = [item for item in metadata["instances"] if item["bus"] == bus]
    return matches[0] if len(matches) == 1 else None


def _limits(field: Mapping[str, Any]) -> tuple[float, float]:
    bits = field["bits"]
    if field.get("signed"):
        raw_minimum, raw_maximum = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
    else:
        raw_minimum, raw_maximum = 0, (1 << bits) - 1
    factor, offset = field.get("factor", 1), field.get("offset", 0)
    return (
        field.get("min", raw_minimum * factor + offset),
        field.get("max", raw_maximum * factor + offset),
    )


def _encode_payload(metadata: Mapping[str, Any], values: Mapping[str, object]) -> tuple[CodecStatus, bytes | None]:
    payload = bytearray(metadata["dlc"])
    for field in metadata["layout"]["fields"]:
        value = field.get("constant", values.get(field["key"]))
        if not isinstance(value, (int, float)) or isinstance(value, bool) or not math.isfinite(value):
            return "value_out_of_range", None
        minimum, maximum = _limits(field)
        if value < minimum or value > maximum:
            return "value_out_of_range", None
        factor, offset = field.get("factor", 1), field.get("offset", 0)
        unrounded = (value - offset) / factor
        raw = round(unrounded)
        if not math.isclose(unrounded, raw, rel_tol=0, abs_tol=1e-9):
            return "value_out_of_range", None
        if "enum" in field and str(raw) not in field["enum"]:
            return "invalid_enum", None
        if field.get("signed") and raw < 0:
            raw += 1 << field["bits"]
        bits = field["bits"]
        if field["bit"] == 0 and bits % 8 == 0:
            width = bits // 8
            payload[field["byte"] : field["byte"] + width] = raw.to_bytes(width, metadata["byte_order"])
        else:
            start = field["byte"] * 8 + field["bit"]
            for offset_index in range(bits):
                position = start + offset_index
                payload[position // 8] |= ((raw >> offset_index) & 1) << (position % 8)
    return "ok", bytes(payload)


def encode(message: str, values: Mapping[str, object], *, bus: str) -> tuple[CodecStatus, Frame | None]:
    try:
        metadata = _message(message)
    except KeyError:
        return "wrong_message_id", None
    instance = _instance(metadata, bus)
    if instance is None:
        return "wrong_message_id", None
    status, payload = _encode_payload(metadata, values)
    if status != "ok":
        return status, None
    return "ok", Frame(bus, instance["id"], instance["frame_format"], payload)


def decode(message: str, frame: Frame) -> tuple[CodecStatus, dict[str, int | float] | None]:
    try:
        metadata = _message(message)
    except KeyError:
        return "wrong_message_id", None
    instance = _instance(metadata, frame.bus)
    if instance is None:
        return "wrong_message_id", None
    status = validate_frame(
        frame,
        bus=instance["bus"],
        can_id=instance["id"],
        frame_format=instance["frame_format"],
        dlc=metadata["dlc"],
    )
    if status != "ok":
        return status, None

    values: dict[str, int | float] = {}
    for field in metadata["layout"]["fields"]:
        bits = field["bits"]
        if field["bit"] == 0 and bits % 8 == 0:
            width = bits // 8
            raw = int.from_bytes(
                frame.data[field["byte"] : field["byte"] + width], metadata["byte_order"]
            )
        else:
            raw = 0
            start = field["byte"] * 8 + field["bit"]
            for offset_index in range(bits):
                position = start + offset_index
                raw |= ((frame.data[position // 8] >> (position % 8)) & 1) << offset_index
        if field.get("signed") and raw & (1 << (bits - 1)):
            raw -= 1 << bits
        if "enum" in field and str(raw) not in field["enum"]:
            return "invalid_enum", None
        value = raw * field.get("factor", 1) + field.get("offset", 0)
        minimum, maximum = _limits(field)
        if value < minimum or value > maximum:
            return "value_out_of_range", None
        if "constant" in field and value != field["constant"]:
            return "constant_mismatch", None
        values[field["key"]] = value
    return "ok", values


def decode_into(
    message: str, frame: Frame, output: MutableMapping[str, object]
) -> CodecStatus:
    status, value = decode(message, frame)
    if status == "ok" and value is not None:
        output.clear()
        output.update(value)
    return status


def decode_payload(
    message: str,
    payload: bytes,
    *,
    bus: str,
    frame_format: FrameFormat,
    can_id: int,
    dlc: int | None = None,
) -> tuple[CodecStatus, dict[str, int | float] | None]:
    return decode(message, Frame(bus, can_id, frame_format, payload, dlc))
