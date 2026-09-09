"""A ready-to-run bench session: link + capture + restbus + inputs + clock.

Constructed by the pytest fixture layer; scenarios consume one ``BenchSession``.
"""
from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

from .can import CanLink, CanalystLink, VirtualLink, open_link
from .capture import Capture
from .clock import Clock
from .config import BenchConfig
from .gpio import BenchIo, GpioBoard, NullGpioBoard, SerialGpioBoard
from .power import BenchPower, NullPower, PowerController, SerialPower

GPIO_PINS = ("estop", "start", "mode", "rt_reset", "sys_reset", "mtr_reset",
             "pwr_rt", "pwr_sys", "pwr_mtr")


@dataclass
class BenchSession:
    cfg: BenchConfig
    link: CanLink
    capture: Capture
    description: str = ""
    clock: Clock = field(default_factory=Clock)
    gpio: Optional[GpioBoard] = None
    power_ctl: Optional[PowerController] = None
    restbus: Optional[object] = None

    @property
    def io(self) -> BenchIo:
        return BenchIo(self.gpio if self.gpio is not None else NullGpioBoard(), self.clock)

    @property
    def power(self) -> BenchPower:
        return BenchPower(self.power_ctl if self.power_ctl is not None else NullPower(), self.clock)

    @property
    def is_physical(self) -> bool:
        return isinstance(self.link, CanalystLink)


def open_gpio(physical: bool = False) -> GpioBoard:
    port = os.environ.get("SYSTEMTEST_GPIO_PORT")
    if port:
        return SerialGpioBoard(port=port, pins=GPIO_PINS)
    board = NullGpioBoard()
    if not physical:
        board.enable_recording()
    return board


def open_power(gpio: GpioBoard, physical: bool = False) -> PowerController:
    if isinstance(gpio, SerialGpioBoard):
        return SerialPower(gpio)
    power = NullPower()
    if not physical:
        power.enable_recording()
    return power


def create_session(cfg: BenchConfig, traces_path: Optional[Path] = None) -> BenchSession:
    from restbus import Restbus  # deferred import keeps the bench package independent

    link, description = open_link(cfg)
    physical = isinstance(link, CanalystLink)
    capture = Capture(link, traces_path=traces_path)
    session = BenchSession(cfg=cfg, link=link, capture=capture, description=description)
    session.gpio = open_gpio(physical=physical)
    session.power_ctl = open_power(session.gpio, physical=physical)
    session.restbus = Restbus(link, capture=capture)
    return session
