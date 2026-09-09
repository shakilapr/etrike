"""Wire facts, message keys, sample model + encode/decode helpers.

All message keys/ids/dlc come from the generated protocol catalog and are
validated at import time so a contract change fails loudly instead of silently
corrupting a bench run.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

from protocol.codecs.python import codec
from protocol.codecs.python.types import Frame as PFrame
from protocol.e2e import DATA_ID_SYS_SAFETY_STS, crc8_h2f
from protocol.generated.python.etrike_protocol import METADATA

# Logical buses as named by the protocol catalog / hardware docs.
HIGH = "high"
LOW = "low"
BUSES = (HIGH, LOW)

# ── message keys the bench asserts on / owns ───────────────────────────
HOST_DRIVE_CMD = "host:host_drive_cmd"          # 0x300 high
HOST_BRAKE_REQ = "host:host_brake_req"          # 0x301 high
HOST_STEER_CMD = "host:host_steer_cmd"          # 0x303 high
HOST_HEARTBEAT = "host:host_heartbeat"          # 0x7FC high
SYS_MODE_CMD = "sys:sys_mode_cmd"               # 0x110 low
SYS_PWR_CMD = "sys:sys_pwr_cmd"                 # 0x113 low
SYS_SAFETY_STS = "sys:sys_safety_sts"           # 0x011 low(+high same frame)
SYS_HEARTBEAT = "sys:sys_heartbeat"             # 0x7FE low
RT_HEARTBEAT = "rt:rt_heartbeat"                # 0x7FD high+low
RT_DRIVE_CMD = "rt:rt_drive_cmd"                # 0x204 low
RT_BRAKE_CMD = "rt:rt_brake_cmd"                # 0x205 low
RT_STATE_RPT = "rt:rt_state_rpt"                # 0x210 high+low
MTR_MOTOR_FBK = "mtr:mtr_motor_fbk"             # 0x206 low(+high same frame)
MTR_NODE_STATUS = "mtr:mtr_node_status"         # 0x502 low+high
RT_NODE_STATUS = "rt:rt_node_status"            # 0x501 low+high
SYS_NODE_STATUS = "sys:sys_node_status"         # 0x500 low(+high same frame)
SAFETY_ESTOP = "safety:safety_estop"            # 0x001 high+low
SEB_REQ = "seb:vcu_seb_req"                     # 0x7B9 low
SEB_STATUS = "seb:seb_status"                   # 0x721 low
SEB_ERR_INFO = "seb:seb_err_info"               # 0x731 low
SES_REQ = "ses:vcu_ses_req"                     # 0x169 low
SES_STATUS = "ses:ses_status"                   # 0x201 low


@dataclass(frozen=True)
class WireKey:
    key: str
    name: str
    dlc: int


def _build_index() -> dict[tuple[str, int], WireKey]:
    index: dict[tuple[str, int], WireKey] = {}
    for message_key, message in METADATA.items():
        for instance in message["instances"]:
            bus = instance["bus"]
            if bus not in BUSES:
                continue
            index[(bus, instance["id"])] = WireKey(
                key=message_key, name=message["name"], dlc=message["dlc"]
            )
    return index


INDEX: dict[tuple[str, int], WireKey] = _build_index()


def _validate() -> None:
    def must(bus: str, can_id: int, message_key: str) -> None:
        hit = INDEX.get((bus, can_id))
        assert hit is not None, f"missing wire fact {bus} 0x{can_id:X}"
        assert hit.key == message_key, f"drift: (bus={bus}, id=0x{can_id:X}) now {hit.key!r}, expected {message_key!r}"

    must(HIGH, 0x300, HOST_DRIVE_CMD)
    must(HIGH, 0x301, HOST_BRAKE_REQ)
    must(HIGH, 0x303, HOST_STEER_CMD)
    must(HIGH, 0x7FC, HOST_HEARTBEAT)
    must(LOW, 0x110, SYS_MODE_CMD)
    must(LOW, 0x113, SYS_PWR_CMD)
    must(LOW, 0x011, SYS_SAFETY_STS)
    must(LOW, 0x7FE, SYS_HEARTBEAT)
    must(LOW, 0x7FD, RT_HEARTBEAT)
    must(HIGH, 0x7FD, RT_HEARTBEAT)
    must(LOW, 0x204, RT_DRIVE_CMD)
    must(LOW, 0x205, RT_BRAKE_CMD)
    must(LOW, 0x210, RT_STATE_RPT)
    must(LOW, 0x206, MTR_MOTOR_FBK)
    must(LOW, 0x502, MTR_NODE_STATUS)
    must(LOW, 0x501, RT_NODE_STATUS)
    must(LOW, 0x500, SYS_NODE_STATUS)
    must(LOW, 0x001, SAFETY_ESTOP)
    must(HIGH, 0x001, SAFETY_ESTOP)
    must(LOW, 0x7B9, SEB_REQ)
    must(LOW, 0x721, SEB_STATUS)
    must(LOW, 0x731, SEB_ERR_INFO)
    must(LOW, 0x169, SES_REQ)
    must(LOW, 0x201, SES_STATUS)


_validate()


def message_key_for(bus: str, can_id: int) -> Optional[str]:
    hit = INDEX.get((bus, can_id))
    return hit.key if hit else None


def encode_message(bus: str, can_id: int, values: dict) -> bytes:
    key = message_key_for(bus, can_id)
    if key is None:
        raise KeyError(f"no catalog entry for {bus} 0x{can_id:X}")
    status, pframe = codec.encode(key, values, bus=bus)
    if status != "ok" or pframe is None:
        raise ValueError(f"encode failed {key}: {status}")
    if pframe.id != can_id:
        raise ValueError(f"encode drift: key {key} produced 0x{pframe.id:X}, wanted 0x{can_id:X}")
    return pframe.data


def decode_message(bus: str, can_id: int, data: bytes) -> tuple[Optional[str], Optional[dict]]:
    key = message_key_for(bus, can_id)
    if key is None:
        return None, None
    pframe = PFrame(bus=bus, id=can_id, frame_format="standard", data=data)
    status, values = codec.decode(key, pframe)
    if status != "ok":
        return key, None
    return key, values


def safety_sts_e2e_ok(payload: bytes) -> bool:
    """True when a 0x011 payload carries a valid AUTOSAR E2E CRC over bytes[0:4]."""
    if len(payload) != 5:
        return False
    return crc8_h2f(payload[:4], DATA_ID_SYS_SAFETY_STS) == payload[4]


@dataclass
class Sample:
    """A decoded frame observation, timestamped at the receive boundary."""

    bus: str
    can_id: int
    key: Optional[str]
    values: Optional[dict]  # None => present on the wire but failed protocol decode
    data: bytes
    ts: float
    hw_ts: Optional[float]

    @property
    def is_valid(self) -> bool:
        return self.key is not None and self.values is not None

    @property
    def name(self) -> Optional[str]:
        hit = INDEX.get((self.bus, self.can_id))
        return hit.name if hit else None

    def to_trace(self) -> dict:
        return {
            "bus": self.bus,
            "id": f"0x{self.can_id:X}",
            "dlc": len(self.data),
            "data": self.data.hex(),
            "key": self.key,
            "name": self.name,
            "decoded": self.values if self.is_valid else None,
            "ts": round(self.ts, 6),
            "hw_ts": round(self.hw_ts, 6) if self.hw_ts is not None else None,
        }
