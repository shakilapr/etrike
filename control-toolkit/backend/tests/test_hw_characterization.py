"""Opt-in, passive CANalyst-II hardware characterization.

This suite never transmits. Run only after the USB-only preflight succeeds:
    $env:CTK_PHYSICAL = "1"
    pytest tests/test_hw_characterization.py -v -m hardware
"""

from __future__ import annotations

import os
import time

import pytest

from control_toolkit.models.adapter import AdapterHealth
from control_toolkit.transport.canalyst import CanalystTransportAdapter, discover_canalyst

pytestmark = pytest.mark.hardware


def test_passive_dual_channel_open_and_listen() -> None:
    if os.getenv("CTK_PHYSICAL") != "1":
        pytest.skip("set CTK_PHYSICAL=1 after connecting CANalyst-II by USB")

    found = discover_canalyst(force=True)
    assert found.available, found.reason

    adapter = CanalystTransportAdapter()
    adapter.open()
    try:
        deadline = time.monotonic() + float(os.getenv("CTK_HW_LISTEN_SECONDS", "3"))
        frames = []
        while time.monotonic() < deadline:
            frames.extend(adapter.poll(timeout=0.1))
        status = adapter.status()
        assert status.health in (
            AdapterHealth.OPEN,
            AdapterHealth.ACTIVE,
            AdapterHealth.QUIET,
        )
        assert status.channel_map == {"high": 0, "low": 1}
        assert status.worker_alive is True
        assert status.channels["high"].rx_overflow == 0
        assert status.channels["low"].rx_overflow == 0
        if os.getenv("CTK_REQUIRE_CAN_TRAFFIC") == "1":
            assert {frame.channel.value for frame in frames} == {"high", "low"}
    finally:
        adapter.close()
