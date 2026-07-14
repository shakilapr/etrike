from __future__ import annotations

from collections.abc import Mapping

from protocol.profiles.xor8_ff_v1 import compute, verify

from .types import CodecStatus, Frame, validate_frame

BUS = "low"
DLC = 8
COMMAND_ID = 0x169
STATUS_ID = 0x201
ERROR_INFO_ID = 0x202
VERSION_ID = 0x203
TEST_ID = 0x6FA


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
    target_angle = _integer(values.get("target_angle_raw"), 0)
    target_speed = _integer(values.get("target_speed_raw"), 328)
    counter = _integer(values.get("rolling_counter"), 0)
    vehicle_speed = _integer(values.get("vehicle_speed_raw"), 0)
    if not isinstance(alignment_enable, bool) or not isinstance(control_enable, bool):
        return "value_out_of_range", None
    if target_angle is None or target_speed is None or counter is None or vehicle_speed is None:
        return "value_out_of_range", None
    if not -0x8000 <= target_angle <= 0x7FFF or not 125 <= target_speed <= 525:
        return "value_out_of_range", None
    if not 0 <= counter <= 15 or not 0 <= vehicle_speed <= 0xFF:
        return "value_out_of_range", None
    payload = bytearray(DLC)
    payload[0] = int(alignment_enable) | (int(control_enable) << 1)
    payload[2:4] = target_angle.to_bytes(2, "little", signed=True)
    payload[4] = target_speed & 0xFF
    payload[5] = 0x03 | ((target_speed >> 6) & 0x0C) | (counter << 4)
    payload[6] = vehicle_speed
    payload[7] = compute(payload[:7])
    return "ok", Frame(BUS, COMMAND_ID, "standard", payload)


def decode_command(frame: Frame) -> tuple[CodecStatus, dict[str, object] | None]:
    status = _validate(frame, COMMAND_ID, checksum=True)
    if status != "ok":
        return status, None
    if frame.data[5] & 0x03 != 0x03:
        return "constant_mismatch", None
    target_speed = frame.data[4] | ((frame.data[5] & 0x0C) << 6)
    if not 125 <= target_speed <= 525:
        return "value_out_of_range", None
    return "ok", {
        "alignment_enable": bool(frame.data[0] & 0x01),
        "control_enable": bool(frame.data[0] & 0x02),
        "target_angle_raw": int.from_bytes(frame.data[2:4], "little", signed=True),
        "target_speed_raw": target_speed,
        "rolling_counter": frame.data[5] >> 4,
        "vehicle_speed_raw": frame.data[6],
    }


def decode_status(frame: Frame) -> tuple[CodecStatus, dict[str, object] | None]:
    status = _validate(frame, STATUS_ID, checksum=True)
    if status != "ok":
        return status, None
    return "ok", {
        "angle_aligned": bool(frame.data[0] & 0x01),
        "control_mode": (frame.data[0] >> 1) & 0x03,
        "error_status": (frame.data[0] >> 6) & 0x03,
        "steering_angle_raw": int.from_bytes(frame.data[2:4], "little"),
        "target_angle_speed_raw": int.from_bytes(frame.data[4:6], "little", signed=True),
        "steering_torque_raw": frame.data[5],
        "rolling_counter_enabled": bool(frame.data[6] & 0x01),
        "checksum_enabled": bool(frame.data[6] & 0x02),
        "rolling_counter": frame.data[6] >> 4,
    }


def decode_error_info(frame: Frame) -> tuple[CodecStatus, dict[str, bytes] | None]:
    status = _validate(frame, ERROR_INFO_ID)
    return (status, {"raw": frame.data} if status == "ok" else None)


def decode_version(frame: Frame) -> tuple[CodecStatus, dict[str, bytes] | None]:
    status = _validate(frame, VERSION_ID)
    if status != "ok":
        return status, None
    return "unsupported_semantics", {"raw": frame.data}


decode_version_raw = decode_version


def decode_test(frame: Frame) -> tuple[CodecStatus, dict[str, int] | None]:
    status = _validate(frame, TEST_ID)
    if status != "ok":
        return status, None
    return "ok", {
        "motor_current_raw": int.from_bytes(frame.data[1:3], "little", signed=True),
        "ecu_temperature_raw": int.from_bytes(frame.data[3:5], "little"),
        "supply_voltage_raw": int.from_bytes(frame.data[5:7], "little"),
    }
