"""Single integration seam to the generated ``protocol`` package.

The Control Toolkit backend never re-implements CAN codecs. It consumes the YAML-generated
Python runtime (``protocol/generated`` + ``protocol/codecs``), which the RT/SYS
firmware also compiles against. The YAML contracts are the source of truth
(see ``control-toolkit/workplan.md`` header); this module is the only place the
backend reaches into that package, and the only place that fixes up sys.path
for the monorepo layout.
"""

from __future__ import annotations

import sys
from functools import lru_cache
from pathlib import Path

# Repo root is three levels up: control-toolkit/backend/control_toolkit/protocol_bridge.py
_REPO_ROOT = Path(__file__).resolve().parents[3]
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

# Imported after the path fix-up so the generated package resolves.
from protocol.codecs.python import (  # noqa: E402
    CodecStatus,
    Frame,
    FrameFormat,
    decode,
    encode,
)
from protocol.generated.python import etrike_protocol  # noqa: E402

#: Content hash of the wire contract. RT/SYS firmware is generated from the same
#: YAML; a mismatch between this and the frontend/firmware means drift.
#: SEMANTIC_HASH is the canonical name; WIRE_HASH is an alias.
SEMANTIC_HASH: str = etrike_protocol.SEMANTIC_HASH
WIRE_HASH: str = etrike_protocol.WIRE_HASH
NETWORK_HASH: str = etrike_protocol.NETWORK_HASH

#: Canonical catalog keyed by ``"owner:key"`` (e.g. ``"rt:rt_heartbeat"``).
CATALOG: dict[str, dict] = etrike_protocol.METADATA

__all__ = [
    "SEMANTIC_HASH",
    "WIRE_HASH",
    "NETWORK_HASH",
    "CATALOG",
    "CodecStatus",
    "Frame",
    "FrameFormat",
    "decode",
    "encode",
    "message_key_for",
    "instance_for",
    "instances",
    "message_count",
    "instance_count",
]


@lru_cache(maxsize=1)
def _bus_id_index() -> dict[tuple[str, int], str]:
    """Map ``(bus, can_id)`` runtime identity -> canonical ``owner:key``.

    A canonical message can appear on more than one bus (e.g. RT_HEARTBEAT on
    High and Low); each physical instance is indexed separately so the pipeline
    can resolve a received frame to its contract.
    """
    index: dict[tuple[str, int], str] = {}
    for key, msg in CATALOG.items():
        for inst in msg.get("instances", []):
            index[(inst["bus"], int(inst["id"]))] = key
    return index


def message_key_for(bus: str, can_id: int) -> str | None:
    """Return the canonical ``owner:key`` for a runtime ``(bus, id)`` or ``None``."""
    return _bus_id_index().get((bus, int(can_id)))


def instance_for(bus: str, can_id: int) -> dict | None:
    """Return the physical instance dict for a runtime ``(bus, id)`` or ``None``."""
    key = message_key_for(bus, can_id)
    if key is None:
        return None
    for inst in CATALOG[key].get("instances", []):
        if inst["bus"] == bus and int(inst["id"]) == int(can_id):
            return inst
    return None


def instances() -> list[dict]:
    """Flat list of every physical instance with its canonical name attached."""
    out: list[dict] = []
    for key, msg in CATALOG.items():
        for inst in msg.get("instances", []):
            out.append({"key": key, "name": msg["name"], **inst})
    return out


def message_count() -> int:
    return len(CATALOG)


def instance_count() -> int:
    return sum(len(m.get("instances", [])) for m in CATALOG.values())
