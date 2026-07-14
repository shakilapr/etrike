from __future__ import annotations

from collections.abc import Mapping

from protocol.profiles.xor8_ff_v1 import compute, verify

from .types import CodecStatus, Frame, validate_frame

BUS = "low"
DLC = 8
COMMAND_ID = 0x7B9
STATUS_ID = 0x721
ERROR_INFO_ID = 0x731
VERSION_ID = 0x741
TEST_ID = 0x6FB


def _validate(frame: Frame, can_id: int, *, checksum: bool = False) -> CodecStatus:
    status = validate_frame(frame, bus=BUS, can_id=can_id, frame_format="standard", dlc=DLC)
    if status != "ok":
        return status
    if checksum and not verify(frame.data[:7], frame.data[7]):
        return "checksum_mismatch"
    return "ok"


def _integer(value: object, default: int) -> int | None:
    selected = default if value is None else value
    return selected if isinstance(selected, int) and not isinstance(selected, bool) else None


def encode_command(values: Mapping[str, object]) -> tuple[CodecStatus, Frame | None]:
    alignment_enable = values.get("alignment_enable", False)
    control_enable = values.get("control_enable", False)
    auto_brake = values.get("auto_brake", False)
    mode = _integer(values.get("control_mode"), 0)
    stroke = _integer(values.get("stroke_request_raw"), 600)
    pressure = _integer(values.get("pressure_request_raw"), 0)
    counter = _integer(values.get("rolling_counter"), 0)
    if not all(isinstance(value, bool) for value in (alignment_enable, control_enable, auto_brake)):
        return "value_out_of_range", None
    if stroke is None or pressure is None or counter is None:
        return "value_out_of_range", None
    if mode not in (0, 1):
        return "invalid_enum", None
    if not 0 <= stroke <= 0xFFFF or not 0 <= pressure <= 100 or not 0 <= counter <= 15:
        return "value_out_of_range", None
    payload = bytearray(DLC)
    payload[0] = (
        int(alignment_enable)
        | (int(control_enable) << 1)
        | (mode << 2)
        | (int(auto_brake) << 3)
    )
    payload[2] = stroke & 0xFF
    payload[3] = stroke >> 8 if mode == 0 else pressure
    payload[6] = 0x03 | (counter << 4)
    payload[7] = compute(payload[:7])
    return "ok", Frame(BUS, COMMAND_ID, "standard", payload)


def decode_command(frame: Frame) -> tuple[CodecStatus, dict[str, object] | None]:
    status = _validate(frame, COMMAND_ID, checksum=True)
    if status != "ok":
        return status, None
    if frame.data[6] & 0x03 != 0x03:
        return "constant_mismatch", None
    mode = 1 if frame.data[0] & 0x04 else 0
    pressure = frame.data[3] if mode == 1 else 0
    if pressure > 100:
        return "value_out_of_range", None
    return "ok", {
        "alignment_enable": bool(frame.data[0] & 0x01),
        "control_enable": bool(frame.data[0] & 0x02),
        "control_mode": mode,
        "auto_brake": bool(frame.data[0] & 0x08),
        "stroke_request_raw": frame.data[2] | (frame.data[3] << 8) if mode == 0 else frame.data[2],
        "pressure_request_raw": pressure,
        "rolling_counter": frame.data[6] >> 4,
    }


def decode_status(frame: Frame) -> tuple[CodecStatus, dict[str, object] | None]:
    status = _validate(frame, STATUS_ID, checksum=True)
    if status != "ok":
        return status, None
    return "ok", {
        "status_byte": frame.data[0],
        "alignment_status": bool(frame.data[0] & 0x01),
        "control_enabled": bool(frame.data[0] & 0x02),
        "control_mode": (frame.data[0] >> 2) & 0x03,
        "auto_brake_status": bool(frame.data[0] & 0x10),
        "error_status": (frame.data[0] >> 6) & 0x03,
        "stroke_value_raw": int.from_bytes(frame.data[2:4], "little"),
        "pressure_value_raw": frame.data[3],
        "angle_value_raw": int.from_bytes(frame.data[5:7], "little", signed=True),
        "rolling_counter_enabled": bool(frame.data[6] & 0x01),
        "checksum_enabled": bool(frame.data[6] & 0x02),
        "rolling_counter": frame.data[6] >> 4,
    }


def decode_error_info(frame: Frame) -> tuple[CodecStatus, dict[str, bytes] | None]:
    status = _validate(frame, ERROR_INFO_ID)
    return (status, {"raw": frame.data} if status == "ok" else None)


def decode_version(frame: Frame) -> tuple[CodecStatus, dict[str, object] | None]:
    status = _validate(frame, VERSION_ID)
    if status != "ok":
        return status, None
    return "ok", {"software_raw": frame.data[0], "hardware_raw": frame.data[1], "raw": frame.data}


def decode_test(frame: Frame) -> tuple[CodecStatus, dict[str, int] | None]:
    status = _validate(frame, TEST_ID)
    if status != "ok":
        return status, None
    return "ok", {
        "motor_current_raw": int.from_bytes(frame.data[1:3], "little", signed=True),
        "ecu_temperature_raw": int.from_bytes(frame.data[3:5], "little"),
        "supply_voltage_raw": int.from_bytes(frame.data[5:7], "little"),
    }
