"""SES / SES (steer-by-wire) restbus model.

Consumes 0x169 VCU_SES_REQ from the *real* RT ECU and reports 0x201 SES_STATUS
(+ 0x202 err info / 0x203 version). Payload mirrors protocol/codecs/python/ses.py
decode_status:
  byte0:  alignment bit0, control_mode bits1-2, error_status bits6-7
  bytes2:3: steering angle u16 LE
  bytes4:5: target angle/speed i16 LE
  byte6:  rolling_counter<<4 | 0x03
  byte7:  XOR8-complement over bytes 0..6

Fault modes: healthy | silent | frozen | stuck | slow | l3_fault
"""
from __future__ import annotations

import time

from bench.can import CanLink
from bench.wire import LOW
from protocol.codecs.python import ses as ses_codec
from protocol.codecs.python.types import Frame
from restbus.base import RestbusNode

SES_REPORT_ID = 0x201
SES_CMD_ID = 0x169
SES_ERR_ID = 0x202
SES_VER_ID = 0x203

ERR_NONE = 0
ERR_L3 = 3


def _xor8ff(data: bytes) -> int:
    checksum = 0
    for value in data:
        checksum ^= value
    return (checksum ^ 0xFF) & 0xFF


def build_status_bytes(
    *,
    angle_raw: int = 0,
    aligned: bool = True,
    control_mode: int = 0,
    error_status: int = ERR_NONE,
    counter: int = 0,
) -> bytes:
    payload = bytearray(8)
    payload[0] = int(aligned) | (control_mode << 1) | (error_status << 6)
    payload[2:4] = angle_raw.to_bytes(2, "little", signed=False)
    payload[4:6] = (0).to_bytes(2, "little", signed=True)
    payload[6] = 0x03 | ((counter & 0x0F) << 4)
    payload[7] = _xor8ff(payload[:7])
    return bytes(payload)


class SesNode(RestbusNode):
    name = "ses"
    bus = LOW
    period_s = 0.010  # SES_STATUS cycle_ms = 10 ms

    def __init__(self, link: CanLink, command_provider=None):
        super().__init__(link, bus=LOW)
        self._command_provider = command_provider
        self.angle_raw = 0
        self.error_status = ERR_NONE
        self.l3_reported = False
        self._version_due = 0.0

    def set_command_provider(self, provider) -> None:
        self._command_provider = provider

    def _emit(self) -> None:
        now = time.perf_counter()
        cmd = self._command_provider() if self._command_provider is not None else None
        if cmd is not None:
            target = int(cmd.get("target_angle_raw") or 0)
            if self.fault_mode == "stuck":
                pass  # keep reporting old angle
            elif self.fault_mode == "slow":
                self.angle_raw += (target - self.angle_raw) // 8
            else:
                self.angle_raw = target
        counter = self.advance("nibble", 16)

        if self.fault_mode == "l3_fault":
            self.error_status = ERR_L3
        elif self.error_status == ERR_L3 and self.fault_mode == "healthy":
            self.error_status = ERR_NONE

        payload = build_status_bytes(
            angle_raw=self.angle_raw,
            aligned=True,
            control_mode=0,
            error_status=self.error_status,
            counter=counter,
        )
        status, decoded = ses_codec.decode_status(
            Frame(bus=LOW, id=SES_REPORT_ID, frame_format="standard", data=payload)
        )
        if status == "ok" and decoded is not None:
            self._link.send(LOW, SES_REPORT_ID, payload)
            self.sent_count[SES_REPORT_ID] = self.sent_count.get(SES_REPORT_ID, 0) + 1
        self.state.update(angle_raw=self.angle_raw, error_status=self.error_status)

        if self.error_status == ERR_L3 and not self.l3_reported:
            err = bytearray(8)
            err[0] = 0x80
            err[7] = _xor8ff(err[:7])
            self._link.send(LOW, SES_ERR_ID, bytes(err))
            self.l3_reported = True

        if now >= self._version_due:
            self._version_due = now + 1.0
            ver = bytearray(8)
            ver[0] = 0x01
            ver[1] = 0x03
            ver[7] = _xor8ff(ver[:7])
            self._link.send(LOW, SES_VER_ID, bytes(ver))
            self.sent_count[SES_VER_ID] = self.sent_count.get(SES_VER_ID, 0) + 1

    def inject_fault(self, mode: str) -> None:
        self.fault_mode = mode
        if mode == "l3_fault":
            self.l3_reported = False
