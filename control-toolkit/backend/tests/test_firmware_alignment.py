"""Cross-check protocol catalog + toolkit limits against firmware contracts.

Sources:
  - protocol/contracts/host.yaml, hmi.yaml, network.yaml
  - shared/shared_config.h
  - mtr-stm32 gear enum N/D/S/R
  - sys-esp32 mode_manager (HMI MANUAL/AUTO only)
"""

from __future__ import annotations

from control_toolkit import protocol_bridge as proto
from control_toolkit.services.control_intent import (
    GEAR_D,
    GEAR_N,
    GEAR_R,
    GEAR_S,
    HOST_CMD_STALE_S,
    HOST_DRIVE_PERIOD_MS,
    MAX_SPEED_FWD_MMPS,
    MAX_SPEED_REV_MMPS,
    MAX_YAW_MRAD_S,
)
from control_toolkit.services.encoder import encode_message


def test_host_drive_id_dlc_and_bounds_match_yaml():
    meta = proto.CATALOG["host:host_drive_cmd"]
    assert meta["name"] == "HOST_DRIVE_CMD"
    assert meta["dlc"] == 8
    inst = meta["instances"][0]
    assert inst["bus"] == "high"
    assert inst["id"] == 0x300
    assert inst["cycle_ms"] == 10  # 100 Hz max
    fields = {f["key"]: f for f in meta["layout"]["fields"]}
    assert fields["speed_mmps"]["min"] == -500
    assert fields["speed_mmps"]["max"] == 3000
    assert fields["yaw_rate_mrad_s"]["min"] == -3000
    assert fields["yaw_rate_mrad_s"]["max"] == 3000
    # Firmware gear: 0=N 1=D 2=S 3=R (not PRND)
    assert fields["gear"]["enum"] == {"0": "N", "1": "D", "2": "S", "3": "R"}
    assert GEAR_N == 0 and GEAR_D == 1 and GEAR_S == 2 and GEAR_R == 3


def test_control_limits_match_shared_config():
    # shared::kMaxSpeedFwdMmps / kMaxSpeedRevMmps
    assert MAX_SPEED_FWD_MMPS == 3000
    assert MAX_SPEED_REV_MMPS == 500
    assert MAX_YAW_MRAD_S == 3000
    # shared::kHostCmdStaleTimeoutMs
    assert HOST_CMD_STALE_S == 0.5
    assert HOST_DRIVE_PERIOD_MS == 10.0


def test_hmi_mode_manual_auto_only_like_sys_firmware():
    meta = proto.CATALOG["hmi:hmi_mode_req"]
    fields = {f["key"]: f for f in meta["layout"]["fields"]}
    assert fields["req_mode"]["max"] == 1
    assert fields["req_mode"]["enum"] == {"0": "MANUAL", "1": "AUTO"}
    # SYS rejects requested_mode > 1 (mode_manager.cpp)
    assert "2" not in fields["req_mode"]["enum"]


def test_estop_dlc0_on_both_buses():
    meta = proto.CATALOG["safety:safety_estop"]
    assert meta["dlc"] == 0
    buses = {i["bus"] for i in meta["instances"]}
    assert buses == {"high", "low"}
    ids = {i["id"] for i in meta["instances"]}
    assert ids == {0x001}


def test_encode_host_drive_matches_golden_vector_shape():
    # From protocol/vectors payload host-drive-signed-i24 gear=3 → R
    enc = encode_message(
        key="host:host_drive_cmd",
        bus="high",
        values={"speed_mmps": 1000, "yaw_rate_mrad_s": -2, "gear": 3},
    )
    assert enc.ok
    assert enc.can_id == 0x300
    assert enc.dlc == 8
    assert len(enc.data) == 8


def test_hmi_and_host_ids_match_architecture_table():
    assert proto.message_key_for("high", 0x111) == "hmi:hmi_mode_req"
    assert proto.message_key_for("high", 0x112) == "hmi:hmi_pwr_req"
    assert proto.message_key_for("high", 0x300) == "host:host_drive_cmd"
    assert proto.message_key_for("high", 0x001) == "safety:safety_estop"
    assert proto.message_key_for("low", 0x001) == "safety:safety_estop"
