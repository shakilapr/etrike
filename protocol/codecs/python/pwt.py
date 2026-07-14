from __future__ import annotations

from collections.abc import Mapping

from .types import CodecStatus, Frame, validate_frame

BUS = "powertrain"
DCDC_COMMAND_ID = 0x10262B27
DLC = 8


def encode_dcdc_command(values: Mapping[str, object]) -> tuple[CodecStatus, Frame | None]:
    control = values.get("control", 1)
    reset = values.get("reset_control", 0)
    if not isinstance(control, int) or isinstance(control, bool):
        return "invalid_enum", None
    if not isinstance(reset, int) or isinstance(reset, bool):
        return "invalid_enum", None
    if control not in (0, 1) or reset not in (0, 1):
        return "invalid_enum", None
    return "ok", Frame(BUS, DCDC_COMMAND_ID, "extended", bytes([control, *([0xFF] * 6), reset]))


def decode_dcdc_command(frame: Frame) -> tuple[CodecStatus, dict[str, int] | None]:
    status = validate_frame(
        frame, bus=BUS, can_id=DCDC_COMMAND_ID, frame_format="extended", dlc=DLC
    )
    if status != "ok":
        return status, None
    if frame.data[0] not in (0, 1) or frame.data[7] not in (0, 1):
        return "invalid_enum", None
    if any(value != 0xFF for value in frame.data[1:7]):
        return "constant_mismatch", None
    return "ok", {
        "control": frame.data[0],
        **{f"reserved_{index}": 0xFF for index in range(1, 7)},
        "reset_control": frame.data[7],
    }
