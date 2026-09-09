"""Host restbus: the PC plays the production HOST on High CAN.

Emits the real Host frames the RT ECU expects:
  * 0x7FC HOST_HEARTBEAT  @ 2 Hz   (alive_ctr + health_flags)
  * 0x300 HOST_DRIVE_CMD  while driving
  * 0x303 HOST_STEER_CMD  while steering
  * 0x301 HOST_BRAKE_REQ  while braking

Drive/steer/brake are asserted only when the corresponding ``*_active`` flag is
on, so a scenario can stop individual streams (e.g. drive timeout during a
corner) by external control — never by internal ECU state.
"""
from __future__ import annotations

import time

from bench.can import CanLink
from bench.wire import HIGH
from restbus.base import RestbusNode


class HostNode(RestbusNode):
    name = "host"
    bus = HIGH
    period_s = 0.005

    def __init__(self, link: CanLink):
        super().__init__(link, bus=HIGH)
        self.hb_period_s = 0.5
        self.cmd_period_s = 0.02
        self.drive_active = False
        self.steer_active = False
        self.brake_active = False
        self.speed_mmps = 0
        self.gear = 1  # D
        self.yaw_rate_mrad_s = 0
        self.steer_angle_0_1deg = 0
        self.brake_pressure_kpa = 0
        self.health_flags = 0
        self._hb_due = 0.0
        self._cmd_due = 0.0

    def set_drive(self, speed_mmps: int, gear: int = 1, active: bool = True, yaw_rate_mrad_s: int = 0) -> None:
        self.speed_mmps = int(speed_mmps)
        self.gear = int(gear)
        self.yaw_rate_mrad_s = int(yaw_rate_mrad_s)
        self.drive_active = bool(active)

    def set_steer(self, angle_0_1deg: int, active: bool = True) -> None:
        self.steer_angle_0_1deg = int(angle_0_1deg)
        self.steer_active = bool(active)

    def set_brake(self, pressure_kpa: int, active: bool = True) -> None:
        self.brake_pressure_kpa = int(pressure_kpa)
        self.brake_active = bool(active)

    def _emit(self) -> None:
        now = time.perf_counter()
        if now >= self._hb_due:
            self._hb_due = now + self.hb_period_s
            self.send(0x7FC, {"alive_ctr": self.advance("hb", 256), "health_flags": self.health_flags})
        if self.drive_active or self.steer_active or self.brake_active:
            if now >= self._cmd_due:
                self._cmd_due = now + self.cmd_period_s
                if self.drive_active:
                    self.send(
                        0x300,
                        {
                            "speed_mmps": self.speed_mmps,
                            "yaw_rate_mrad_s": self.yaw_rate_mrad_s,
                            "gear": self.gear,
                        },
                    )
                if self.steer_active:
                    self.send(
                        0x303,
                        {
                            "steer_angle_0_1deg": self.steer_angle_0_1deg,
                            "angle_valid": 1,
                            "rolling_counter": self.advance("steer", 256),
                        },
                    )
                if self.brake_active:
                    self.send(
                        0x301,
                        {"brake_pressure_kpa": self.brake_pressure_kpa},
                    )
