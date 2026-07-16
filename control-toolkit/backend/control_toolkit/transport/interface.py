"""Transport protocol shared by virtual and CANalyst-II adapters.

One common contract (python-can style) across physical and virtual interfaces.
Blocking receive runs in a dedicated thread (``can.Notifier``); its listener does
a constant-time copy of each frame into a bounded queue and never decodes. The
router (§1.4) drains that queue via :meth:`Transport.poll` off the ASGI loop.
"""

from __future__ import annotations

from typing import Protocol, runtime_checkable

from control_toolkit.models.adapter import AdapterStatus, Capability
from control_toolkit.models.frames import RawFrameEnvelope


@runtime_checkable
class Transport(Protocol):
    """Abstract CAN transport. Implementations: VirtualTransportAdapter (§1.3),
    CanalystTransportAdapter (§2.1)."""

    @property
    def capability(self) -> Capability:
        """Static capability record; unsupported metrics are Unknown (None)."""
        ...

    def status(self) -> AdapterStatus:
        """Current adapter/channel health snapshot."""
        ...

    def open(self) -> None:
        """Open channels and start the receive worker. Assigns a new epoch."""
        ...

    def poll(self, max_items: int = 256, timeout: float = 0.0) -> list[RawFrameEnvelope]:
        """Drain up to ``max_items`` frames from the bounded RX queue.

        Blocks up to ``timeout`` seconds for the first frame, then drains what is
        immediately available. Never decodes; returns raw envelopes.
        """
        ...

    def send(self, frame: RawFrameEnvelope) -> str:
        """Submit a frame for transmission. Returns a disposition; 'submitted'
        never means 'delivered' (can-analyzer-research §3)."""
        ...

    def close(self) -> None:
        """Idempotent shutdown; stops the receive worker and periodic tasks."""
        ...
