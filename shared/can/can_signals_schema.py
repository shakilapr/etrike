"""
pydantic models for E-Trike CAN signal dictionary YAML.

Validates can_signals.yaml before it's passed to canmatrix for DBC generation.
"""

from __future__ import annotations
from enum import Enum
from typing import Optional, Literal
from pydantic import BaseModel, Field, field_validator, model_validator
import yaml
from pathlib import Path


# ── Enums ─────────────────────────────────────────────────────────────

class ByteOrder(str, Enum):
    motorola = "motorola"   # big-endian (MSB first), DBC @0
    intel = "intel"         # little-endian (LSB first), DBC @1

class SignalType(str, Enum):
    signed = "signed"
    unsigned = "unsigned"


# ── Models ────────────────────────────────────────────────────────────

class EcuDef(BaseModel):
    """An ECU/node on the CAN bus."""
    name: str = Field(..., min_length=1, max_length=64,
                      pattern=r'^[A-Z][A-Za-z0-9_]*$')
    comment: str = ""


class SignalDef(BaseModel):
    """One signal within a CAN message.

    Uses byte/bit_offset notation instead of 0-63 start_bit:
      byte=1, bit_offset=0 means "starts at bit 0 of byte 1 in the CAN payload."
      bit_offset is 0=LSB through 7=MSB within the byte.
      The generator computes the canmatrix start_bit from (byte, bit_offset, size, endianness).
    """
    name: str = Field(..., min_length=1, max_length=64,
                      pattern=r'^[A-Z][A-Za-z0-9_]*$')
    byte: int = Field(..., ge=0, le=63,
                      description="Byte index within the CAN payload (0-based)")
    bit_offset: int = Field(..., ge=0, le=7,
                            description="Bit position within the byte (0=LSB, 7=MSB)")
    size: int = Field(..., ge=1, le=64,
                      description="Signal width in bits")
    type: SignalType = SignalType.unsigned
    factor: float = 1.0
    offset: float = 0.0
    min: Optional[float] = Field(None, description="Auto-computed from bit range if None")
    max: Optional[float] = Field(None, description="Auto-computed from bit range if None")
    unit: str = ""
    receivers: list[str] = Field(default_factory=list)
    comment: str = ""
    values: Optional[dict[int, str]] = Field(None, description="Value table for enums")

    @field_validator("values", mode="before")
    @classmethod
    def coerce_value_keys(cls, v):
        """YAML keys are always strings; coerce to int."""
        if v is None:
            return v
        if isinstance(v, dict):
            return {int(k) if not isinstance(k, int) else k: str(val) for k, val in v.items()}
        return v

    @model_validator(mode="after")
    def compute_min_max(self):
        """Auto-compute min/max from bit range if not specified."""
        if self.min is None:
            if self.type == SignalType.signed:
                self.min = float(-(1 << (self.size - 1)))
            else:
                self.min = 0.0
        if self.max is None:
            if self.type == SignalType.signed:
                self.max = float((1 << (self.size - 1)) - 1)
            else:
                self.max = float((1 << self.size) - 1)
        return self

    def compute_start_bit(self, byte_order: ByteOrder) -> int:
        """Compute the canmatrix start_bit (LSB convention) from byte/bit_offset.

        For both Motorola and Intel, canmatrix uses the DBC LSB convention:
        start_bit = byte * 8 + bit_offset where bit_offset=0 is the LSB of that byte.
        canmatrix handles the Motorola-to-MSB output conversion internally.
        """
        return self.byte * 8 + self.bit_offset


class MessageDef(BaseModel):
    """One CAN message (frame)."""
    id: int = Field(..., ge=0, le=0x7FF)
    name: str = Field(..., min_length=1, max_length=64,
                      pattern=r'^[A-Z][A-Za-z0-9_]*$')
    dlc: int = Field(..., ge=0, le=64, description="Data length in bytes")
    sender: str
    receivers: list[str] = Field(default_factory=list)
    cycle_ms: int = Field(0, ge=0)
    comment: str = ""
    signals: list[SignalDef] = Field(default_factory=list)

    @field_validator("id", mode="before")
    @classmethod
    def coerce_hex_id(cls, v):
        """Accept hex strings like '0x204'."""
        if isinstance(v, str):
            return int(v, 16) if v.startswith("0x") else int(v)
        return v

    @model_validator(mode="after")
    def check_signal_bit_bounds(self):
        """Check no signal exceeds frame DLC."""
        if self.dlc == 0:
            return self
        for sig in self.signals:
            # The last byte occupied is roughly start_bit/8 + size/8
            start_bit = sig.byte * 8 + sig.bit_offset
            end_bit = start_bit + sig.size - 1
            end_byte = end_bit // 8
            if end_byte >= self.dlc:
                raise ValueError(
                    f"Signal '{sig.name}' in message '{self.name}' (0x{self.id:03X}): "
                    f"occupies up to byte {end_byte} but DLC={self.dlc}"
                )
        return self


class ProtocolDef(BaseModel):
    """One CAN protocol section — maps to one DBC file."""
    byte_order: ByteOrder
    bus: str = ""
    output: str = Field(..., description="Output .dbc path relative to repo root")
    messages: list[MessageDef] = Field(default_factory=list)

    @model_validator(mode="after")
    def check_unique_message_ids(self):
        """Warn on duplicate IDs (forwarded frames intentionally share IDs)."""
        from warnings import warn
        ids_seen: dict[int, str] = {}
        for msg in self.messages:
            if msg.id in ids_seen:
                warn(f"Duplicate CAN ID 0x{msg.id:03X}: "
                     f"'{ids_seen[msg.id]}' and '{msg.name}' "
                     f"(may be intentional for forwarded frames)")
            ids_seen[msg.id] = msg.name
        return self

    @model_validator(mode="after")
    def check_unique_message_names(self):
        """Error on duplicate message names within a protocol."""
        names = [m.name for m in self.messages]
        dupes = {n for n in names if names.count(n) > 1}
        if dupes:
            raise ValueError(f"Duplicate message names in protocol: {dupes}")
        return self

    @model_validator(mode="after")
    def check_unique_signal_names(self):
        """Error on duplicate signal names within a message."""
        for msg in self.messages:
            names = [s.name for s in msg.signals]
            dupes = {n for n in names if names.count(n) > 1}
            if dupes:
                raise ValueError(
                    f"Duplicate signal names in message '{msg.name}': {dupes}"
                )
        return self


class CanDatabase(BaseModel):
    """Root model for the CAN signal dictionary YAML."""
    can_version: str = "1.0"
    description: str = ""
    constants: dict[str, float | int | str] = Field(default_factory=dict)
    ecus: list[EcuDef] = Field(default_factory=list)
    protocols: dict[str, ProtocolDef]

    @model_validator(mode="after")
    def validate_ecu_references(self):
        """All sender/receiver names must reference declared ECUs."""
        ecu_names = {e.name for e in self.ecus}
        for pname, proto in self.protocols.items():
            for msg in proto.messages:
                if msg.sender not in ecu_names:
                    raise ValueError(
                        f"[{pname}] message '{msg.name}': "
                        f"sender '{msg.sender}' not in declared ECUs: {ecu_names}"
                    )
                for sig in msg.signals:
                    for recv in sig.receivers:
                        if recv not in ecu_names:
                            raise ValueError(
                                f"[{pname}] message '{msg.name}', "
                                f"signal '{sig.name}': "
                                f"receiver '{recv}' not in declared ECUs: {ecu_names}"
                            )
        return self


# ── Loader ────────────────────────────────────────────────────────────

def load_can_database(path: str | Path) -> CanDatabase:
    """Load and validate a CAN database YAML file."""
    with open(path, encoding="utf-8") as f:
        data = yaml.safe_load(f)
    if not isinstance(data, dict) or "can_version" not in data:
        raise ValueError(
            f"Missing 'can_version' key — is '{path}' a valid CAN signals YAML?"
        )
    return CanDatabase.model_validate(data)


def dump_signal_summary(db: CanDatabase) -> str:
    """Produce a human-readable signal table (for --summary flag)."""
    lines = []
    for pname, proto in db.protocols.items():
        lines.append(f"\n{'='*70}")
        lines.append(f"  {pname}  ({proto.byte_order.value}, bus={proto.bus})")
        lines.append(f"{'='*70}")
        for msg in proto.messages:
            lines.append(f"\n  0x{msg.id:03X}  {msg.name}  "
                         f"DLC={msg.dlc}  {msg.cycle_ms}ms  {msg.sender}")
            if msg.comment:
                lines.append(f"    {msg.comment}")
            if not msg.signals:
                lines.append("    (no signals — DLC=0 event frame)")
                continue
            for sig in msg.signals:
                vtable = ""
                if sig.values:
                    items = [f"{k}={v}" for k, v in sorted(sig.values.items())]
                    vtable = f"  {{{', '.join(items)}}}"
                lines.append(
                    f"    {sig.name:32s}  "
                    f"B{sig.byte}.{sig.bit_offset}  "
                    f"sz={sig.size:2d}  "
                    f"{'S' if sig.type == SignalType.signed else 'U'}  "
                    f"({sig.factor:g},{sig.offset:g})  "
                    f"[{sig.min:g},{sig.max:g}]  "
                    f"{sig.unit:8s}  "
                    f"-> {','.join(sig.receivers) if sig.receivers else 'all'}"
                    f"{vtable}"
                )
    return "\n".join(lines)
