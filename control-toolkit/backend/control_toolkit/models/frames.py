"""Immutable frame types (workplan §1.2).

A ``RawFrameEnvelope`` is what the transport layer produces: an exact, immutable
record of one CAN frame observation or transmission. Decoding happens later in
the pipeline and never mutates the envelope — decode failure must preserve the
raw frame and fabricate no values (workplan §1.4).
"""

from __future__ import annotations

from enum import Enum

from pydantic import BaseModel, Field, field_validator


class ChannelId(str, Enum):
    """Physical channel. Ch0 = High, Ch1 = Low (corrected from debug-tool)."""

    HIGH = "high"
    LOW = "low"


class Direction(str, Enum):
    RX = "rx"
    TX = "tx"


class FrameSource(str, Enum):
    """Provenance of a frame. Synthetic TX must never be mistaken for observed RX."""

    PHYSICAL = "physical"
    VIRTUAL = "virtual"
    SYNTHETIC = "synthetic"
    INJECTION = "injection"


class Severity(str, Enum):
    INFO = "info"
    WARNING = "warning"
    ERROR = "error"
    CRITICAL = "critical"


class RawFrameEnvelope(BaseModel):
    """Immutable raw CAN frame observation/transmission.

    ``data`` holds exactly ``dlc`` bytes — never adapter padding (the CANalyst-II
    backend returns padded storage; consumers must respect DLC).
    """

    model_config = {"frozen": True}

    adapter_epoch: int
    channel: ChannelId
    # Raw device timestamp (100 μs units on CANalyst-II); None when unsupported.
    device_timestamp: int | None = None
    # Backend monotonic arrival time in nanoseconds (time.monotonic_ns()).
    backend_arrival_ns: int

    can_id: int
    is_extended: bool = False
    is_remote: bool = False
    dlc: int = Field(ge=0, le=8)
    data: bytes = b""

    # Per-channel sequence; global monotonic sequence assigned by the router.
    channel_sequence: int
    global_sequence: int | None = None

    direction: Direction = Direction.RX
    source: FrameSource = FrameSource.PHYSICAL

    @field_validator("data")
    @classmethod
    def _coerce_bytes(cls, v: object) -> bytes:
        if isinstance(v, (bytes, bytearray)):
            return bytes(v)
        if isinstance(v, list):
            return bytes(v)
        raise TypeError("data must be bytes-like")

    def model_post_init(self, _ctx: object) -> None:
        if len(self.data) != self.dlc:
            raise ValueError(
                f"data length {len(self.data)} != dlc {self.dlc} "
                "(envelope must carry exactly DLC bytes, no padding)"
            )


class TransportEvent(BaseModel):
    """A transport-layer event (adapter/channel health, overflow, loss)."""

    model_config = {"frozen": True}

    severity: Severity
    channel: ChannelId | None = None
    code: str
    detail: str = ""
    evidence: dict = Field(default_factory=dict)
    monotonic_ns: int
    adapter_epoch: int | None = None
