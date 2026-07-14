from __future__ import annotations

from dataclasses import dataclass
from typing import Literal, TypeAlias

CodecStatus: TypeAlias = Literal[
    "ok",
    "wrong_message_id",
    "wrong_frame_format",
    "unexpected_length",
    "value_out_of_range",
    "invalid_enum",
    "constant_mismatch",
    "checksum_mismatch",
    "unsupported_semantics",
]
FrameFormat: TypeAlias = Literal["standard", "extended"]


@dataclass(frozen=True, slots=True)
class Frame:
    bus: str
    id: int
    frame_format: FrameFormat
    data: bytes
    dlc: int | None = None

    def __post_init__(self) -> None:
        object.__setattr__(self, "data", bytes(self.data))
        if self.dlc is None:
            object.__setattr__(self, "dlc", len(self.data))


def validate_frame(
    frame: Frame,
    *,
    bus: str,
    can_id: int,
    frame_format: FrameFormat,
    dlc: int,
) -> CodecStatus:
    if frame.bus != bus or frame.id != can_id:
        return "wrong_message_id"
    if frame.frame_format != frame_format:
        return "wrong_frame_format"
    if frame.dlc != dlc or len(frame.data) != dlc:
        return "unexpected_length"
    return "ok"
