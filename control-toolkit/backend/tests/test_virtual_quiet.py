"""Virtual CAN activity must age to quiet when traffic stops (topbar truthfulness)."""

from __future__ import annotations

import time

from control_toolkit.models.adapter import AdapterHealth, ChannelActivity
from control_toolkit.models.frames import ChannelId
from control_toolkit.transport.virtual import VirtualTransportAdapter


def test_virtual_bus_activity_becomes_quiet_after_idle() -> None:
    adapter = VirtualTransportAdapter(quiet_after_ms=120)
    adapter.open()
    try:
        adapter.inject(ChannelId.HIGH, 0x123, b"\x01\x02")
        # Drain notifier path
        deadline = time.time() + 1.0
        while time.time() < deadline:
            frames = adapter.poll(max_items=16)
            if frames:
                break
            time.sleep(0.01)
        st = adapter.status()
        assert st.channels["high"].activity is ChannelActivity.ACTIVE

        time.sleep(0.2)
        quiet = adapter.status()
        assert quiet.channels["high"].activity is ChannelActivity.QUIET
        assert quiet.health in (AdapterHealth.QUIET, AdapterHealth.OPEN)
    finally:
        adapter.close()
