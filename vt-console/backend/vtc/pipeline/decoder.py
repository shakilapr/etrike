"""Generated-codec decode integration (workplan §1.4).

Thin wrapper over :mod:`vtc.protocol_bridge`. Resolves a raw envelope's
``(bus, can_id)`` to its canonical contract and decodes via the YAML-generated
codec. Unknown frames and decode failures return no fabricated values — the raw
frame stays authoritative.
"""

from __future__ import annotations

from vtc import protocol_bridge as proto
from vtc.models.frames import RawFrameEnvelope


class DecodeResult:
    __slots__ = ("key", "name", "status", "signals")

    def __init__(
        self,
        key: str | None,
        name: str | None,
        status: str,
        signals: dict | None,
    ) -> None:
        self.key = key
        self.name = name
        self.status = status  # 'ok', 'unknown_id', or a CodecStatus error
        self.signals = signals

    @property
    def is_known(self) -> bool:
        return self.key is not None


def decode_envelope(env: RawFrameEnvelope) -> DecodeResult:
    """Decode a raw envelope; never raises on unknown/invalid frames."""
    key = proto.message_key_for(env.channel.value, env.can_id)
    if key is None:
        return DecodeResult(None, None, "unknown_id", None)

    frame = proto.Frame(
        bus=env.channel.value,
        id=env.can_id,
        frame_format="extended" if env.is_extended else "standard",
        data=env.data,
    )
    status, values = proto.decode(key, frame)
    name = proto.CATALOG[key]["name"]
    return DecodeResult(key, name, status, values)
