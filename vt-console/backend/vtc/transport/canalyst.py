"""CANalyst-II physical transport adapter (workplan §2.1 — Phase 2).

Placeholder so the transport package is complete. Phase 2 implements:
- python-can CANalystIIBus, Channel 0 -> High, Channel 1 -> Low (corrected).
- Configurable 1-2ms poll (not the 20ms default), device-timestamp preservation.
- Observable overflow counter (never silent deque(maxlen) eviction).
- Health FSM, disconnect/reconnect with new epoch, capability record.
"""

from __future__ import annotations


class CanalystTransportAdapter:  # pragma: no cover - Phase 2
    def __init__(self) -> None:
        raise NotImplementedError("CanalystTransportAdapter — workplan Phase 2 §2.1")
