"""SEB (Electro-Mechanical Brake) restbus model.

Believable behavioural CAN model, not physics: it consumes 0x7B9 VCU_SEB_REQ
and reports 0x721 SEB_STATUS (+ 0x731 err info / 0x741 version) so the *real*
SYS firmware sees a realistic brake node.

Payload layout mirrors protocol/codecs/python/seb.py decode_status:
  byte0: status_byte (control_enabled bit1, control_mode bits2-3, auto_brake
         bit4, error_status bits6-7)
  bytes2:3: stroke raw u16 LE  (byte3 doubles as vendor pressure overlap)
  bytes5:7: angle i16 LE
  byte6:  rolling_counter<<4 | 0x03 (counter+checksum enabled)
  byte7:  XOR8-complement over bytes 0..6

Fault modes (boundary-level, via ``fault_mode``):
  healthy | silent | frozen | stuck | slow | l3_fault
"""
from __future__ import annotations

import time

from bench.can import CanLink
from bench.wire import LOW, SEB_ERR_INFO
from protocol.codecs.python import seb as seb_codec
from protocol.codecs.python.types import Frame
from restbus.base import FAULT_HEALTHY, RestbusNode

SEB_REPORT_ID = 0x721
SEB_CMD_ID = 0x7B9
SEB_ERR_ID = 0x731
SEB_VER_ID = 0x741

STROKE_0MM = 600  # vendor raw zero-stroke baseline (600 = 0 mm)

# error_status (byte0 bits 6-7) per SYS firmware F7 classification
ERR_NONE = 0
ERR_L2 = 2
ERR_L3 = 3


def _xor8ff(data: bytes) -> int:
    checksum = 0
    for value in data:
        checksum ^= value
    return (checksum ^ 0xFF) & 0xFF


def build_status_bytes(
    *,
    control_enabled: bool = True,
    auto_brake: bool = False,
    error_status: int = ERR_NONE,
    stroke_raw: int = STROKE_0MM,
    angle_raw: int = 0,
    counter: int = 0,
) -> bytes:
    payload = bytearray(8)
    payload[0] = (int(control_enabled) << 1) | (int(auto_brake) << 4) | (error_status << 6)
    payload[2] = stroke_raw & 0xFF
    payload[3] = (stroke_raw >> 8) & 0xFF
    payload[5:7] = angle_raw.to_bytes(2, "little", signed=True)
    payload[6] = 0x03 | ((counter & 0x0F) << 4)
    payload[7] = _xor8ff(payload[:7])
    return bytes(payload)


class SebNode(RestbusNode):
    name = "seb"
    bus = LOW
    period_s = 0.010  # SEB_STATUS cycle_ms = 10 ms

    def __init__(self, link: CanLink, command_provider=None):
        super().__init__(link, bus=LOW)
        self._command_provider = command_provider  # callable() -> decoded 0x7B9 values | None
        self.stroke_raw = STROKE_0MM
        self.angle_raw = 0
        self.error_status = ERR_NONE
        self.l3_reported = False
        self._counter = 0
        self._version_due = 0.0

    def set_command_provider(self, provider) -> None:
        self._command_provider = provider

    def _command(self) -> dict | None:
        if self._command_provider is None:
            return None
        return self._command_provider()

    def _tick_stroke(self, target: int) -> int:
        if self.fault_mode == "stuck":
            return self.stroke_raw  # actuator refuses to move
        if self.fault_mode == "slow":
            self.stroke_raw += (target - self.stroke_raw) // 8
            return self.stroke_raw
        return target

    def _emit(self) -> None:
        now = time.perf_counter()
        cmd = self._command()
        if cmd is not None:
            control_mode = int(cmd.get("control_mode") or 0)
            auto_brake = bool(cmd.get("auto_brake"))
            if control_mode == 0:  # stroke mode
                self.stroke_raw = self._tick_stroke(int(cmd.get("stroke_request_raw") or STROKE_0MM))
        # roll the vendor nibble counter on every healthy report
        counter = self.advance("nibble", 16)

        if self.fault_mode == "l3_fault":
            self.error_status = ERR_L3
            self.stroke_raw = STROKE_0MM
        elif self.error_status == ERR_L3 and self.fault_mode == FAULT_HEALTHY:
            self.error_status = ERR_NONE

        payload = build_status_bytes(
            control_enabled=True,
            auto_brake=False,
            error_status=self.error_status,
            stroke_raw=self.stroke_raw,
            angle_raw=self.angle_raw,
            counter=counter,
        )
        # keep protocol parity: status must round-trip through the vendor decoder
        status, decoded = seb_codec.decode_status(
            Frame(bus=LOW, id=SEB_REPORT_ID, frame_format="standard", data=payload)
        )
        if status == "ok" and decoded is not None:
            self._link.send(LOW, SEB_REPORT_ID, payload)
            self.sent_count[SEB_REPORT_ID] = self.sent_count.get(SEB_REPORT_ID, 0) + 1
        self.state.update(stroke_raw=self.stroke_raw, error_status=self.error_status)

        if self.error_status == ERR_L3 and not self.l3_reported:
            # 0x731 err-info with L3 fault set -> SYS triggers ESTOP from this frame
            err = bytearray(8)
            err[0] = 0x80  # L3-present vendor bit (SYS kL3Bits[0] surface)
            err[7] = _xor8ff(err[:7])
            self._link.send(LOW, SEB_ERR_ID, bytes(err))
            self.l3_reported = True
            self.sent_count[SEB_ERR_ID] = self.sent_count.get(SEB_ERR_ID, 0) + 1

        if now >= self._version_due:
            self._version_due = now + 1.0
            ver = bytearray(8)
            ver[0] = 0x01
            ver[1] = 0x02
            ver[7] = _xor8ff(ver[:7])
            self._link.send(LOW, SEB_VER_ID, bytes(ver))
            self.sent_count[SEB_VER_ID] = self.sent_count.get(SEB_VER_ID, 0) + 1

    def inject_fault(self, mode: str) -> None:
        """Boundary fault switch: healthy | silent | frozen | stuck | slow | l3_fault."""
        self.fault_mode = mode
        if mode == "l3_fault":
            self.l3_reported = False  # allow a fresh 0x731 on the next tick
