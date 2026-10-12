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


def test_decodes_ses_status_checksum_profiles():
    # 0x201 SES_STATUS: 8 bytes
    # Zero security (checksum disabled in byte 6 bit 1)
    env_zero = _env(ChannelId.LOW, 0x201, "0175300000000000")
    res_zero = decode_envelope(env_zero)
    assert res_zero.status == "ok"
    assert res_zero.signals["steering_angle_raw"] == 30000
    assert not res_zero.signals["checksum_enabled"]

    # XOR8 checksum enabled
    env_xor = _env(ChannelId.LOW, 0x201, "01753000000002b9")
    res_xor = decode_envelope(env_xor)
    assert res_xor.status == "ok"
    assert res_xor.signals["checksum_enabled"]

    # Sum8 additive checksum enabled
    env_sum = _env(ChannelId.LOW, 0x201, "01753000000002a8")
    res_sum = decode_envelope(env_sum)
    assert res_sum.status == "ok"
    assert res_sum.signals["checksum_enabled"]

    # Corrupted checksum under checksum_enabled=True
    env_bad = _env(ChannelId.LOW, 0x201, "0175300000000255")
    res_bad = decode_envelope(env_bad)
    assert res_bad.status == "checksum_mismatch"

