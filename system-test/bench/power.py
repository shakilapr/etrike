"""ECU power / reset control for the bench.

Reuses the same serial-JSON GPIO board as ``bench.gpio`` (power FETs and reset
lines) so a scenario can power-cycle an individual ECU — required for REARM and
for the 140-item "recovery-vs-power-cycle" and "physical disconnect" cases.
"""
from __future__ import annotations

import logging
import time
from abc import ABC, abstractmethod
from dataclasses import dataclass
from typing import Optional

log = logging.getLogger("system-test.power")


@dataclass
class RailAction:
    rail: str  # rt | sys | mtr | all
    action: str  # off | on | reset
    ts: float = 0.0

    def to_dict(self) -> dict:
        return {"rail": self.rail, "action": self.action, "ts": round(self.ts, 3)}


class PowerController(ABC):
    @abstractmethod
    def set(self, rail: str, on: bool) -> None: ...

    @abstractmethod
    def cycle(self, rail: str, off_ms: int = 3000) -> None: ...

    @abstractmethod
    def reset(self, rail: str) -> None: ...

    @abstractmethod
    def close(self) -> None: ...


class NullPower(PowerController):
    """No power hardware: raise unless recording-only (self-test / dry run)."""

    def __init__(self) -> None:
        self.history: list[RailAction] = []
        self._allowed = False

    def enable_recording(self) -> None:
        self._allowed = True

    def _record(self, rail: str, action: str) -> None:
        entry = RailAction(rail=rail, action=action, ts=time.perf_counter())
        self.history.append(entry)
        if not self._allowed:
            raise RuntimeError(
                f"no power hardware: cannot {action} rail {rail!r} "
                "(attach a relay/power board or run recording-only)"
            )

    def set(self, rail: str, on: bool) -> None:
        self._record(rail, "on" if on else "off")

    def cycle(self, rail: str, off_ms: int = 3000) -> None:
        self._record(rail, "off")
        time.sleep(off_ms / 1000.0)
        self._record(rail, "on")

    def reset(self, rail: str) -> None:
        self._record(rail, "reset")

    def close(self) -> None: ...


class SerialPower(PowerController):
    """Power FETs / relays controlled through the serial-JSON GPIO board."""

    def __init__(self, gpio_board) -> None:
        self._board = gpio_board
        self.history: list[RailAction] = []

    def _write(self, rail: str, pin_action: str) -> None:
        from .gpio import GpioAction

        self._board.write(GpioAction(pin=f"pwr_{rail}", action=pin_action))
        self.history.append(RailAction(rail=rail, action=pin_action, ts=time.perf_counter()))

    def set(self, rail: str, on: bool) -> None:
        self._write(rail, "press" if on else "release")

    def cycle(self, rail: str, off_ms: int = 3000) -> None:
        self._write(rail, "release")
        time.sleep(off_ms / 1000.0)
        self._write(rail, "press")

    def reset(self, rail: str) -> None:
        self._write(rail, "pulse")

    def close(self) -> None: ...


class BenchPower:
    def __init__(self, controller: PowerController, clock) -> None:
        self._ctl = controller
        self._clock = clock

    @property
    def controller(self) -> PowerController:
        return self._ctl

    def off(self, rail: str) -> None:
        self._ctl.set(rail, False)

    def on(self, rail: str) -> None:
        self._ctl.set(rail, True)

    def cycle(self, rail: str, off_ms: int = 3000) -> None:
        self._ctl.cycle(rail, off_ms=off_ms)

    def reset(self, rail: str) -> None:
        self._ctl.reset(rail)

    def history(self) -> list[dict]:
        return [a.to_dict() for a in self._ctl.history]
