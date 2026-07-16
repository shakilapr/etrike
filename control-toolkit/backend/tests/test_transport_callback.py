"""Prove transport receive callback does not decode (workplan exit gate)."""

from __future__ import annotations

import time
from unittest.mock import patch

from control_toolkit.models.frames import ChannelId
from control_toolkit.pipeline import decoder as decoder_mod
from control_toolkit.transport.virtual import VirtualTransportAdapter


def test_virtual_callback_does_not_call_decode():
    """inject → Notifier listener must not invoke decode_envelope."""
    adapter = VirtualTransportAdapter(rx_queue_maxsize=64)
    adapter.open()
    try:
        with patch.object(
            decoder_mod, "decode_envelope", side_effect=AssertionError("decode in callback")
        ):
            # Callback path only enqueues; decode is not imported by transport.
            adapter.inject(ChannelId.HIGH, 0x300, b"\x00" * 8)
            # Allow notifier thread to deliver.
            deadline = time.monotonic() + 2.0
            frames = []
            while time.monotonic() < deadline:
                frames.extend(adapter.poll(timeout=0.05))
                if frames:
                    break
            assert frames, "frame never arrived on RX queue"
            assert frames[0].can_id == 0x300
            assert frames[0].dlc == 8
            # Raw payload preserved; no decoded fields on envelope.
            assert frames[0].data == b"\x00" * 8
    finally:
        adapter.close()
