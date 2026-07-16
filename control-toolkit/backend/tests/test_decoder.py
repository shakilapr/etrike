"""Pipeline decode path against a Phase 0 golden vector (workplan §1.4).

Proves the backend decodes a real frame through the generated codec via the
protocol bridge, and that unknown frames are preserved without fabricated values.
"""

from __future__ import annotations

from control_toolkit.models.frames import ChannelId, RawFrameEnvelope
from control_toolkit.pipeline.decoder import decode_envelope


def _env(bus: ChannelId, can_id: int, payload_hex: str) -> RawFrameEnvelope:
    data = bytes.fromhex(payload_hex)
    return RawFrameEnvelope(
        adapter_epoch=1,
        channel=bus,
        backend_arrival_ns=1,
        can_id=can_id,
        dlc=len(data),
        data=data,
        channel_sequence=0,
    )


def test_decodes_sys_heartbeat_golden_frame():
    # Golden vector 'sys-heartbeat-all': SYS_HEARTBEAT 0x7FE low, payload ffff.
    env = _env(ChannelId.LOW, 0x7FE, "ffff")
    result = decode_envelope(env)
    assert result.is_known
    assert result.key == "sys:sys_heartbeat"
    assert result.name == "SYS_HEARTBEAT"
    assert result.status == "ok"
    assert result.signals["alive_ctr"] == 255
    assert result.signals["can_ok"] == 1
    assert result.signals["task_can_tx_ok"] == 1


def test_unknown_frame_preserved_without_values():
    env = _env(ChannelId.LOW, 0x123, "0011")
    result = decode_envelope(env)
    assert not result.is_known
    assert result.key is None
    assert result.status == "unknown_id"
    assert result.signals is None
