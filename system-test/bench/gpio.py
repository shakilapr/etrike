"""Physical operator-input control (ESTOP / START / MODE / resets).

The production reset architecture is operator-driven: CAN mode commands cannot
clear the SYS latch, so the bench must actuate the physical SYS/MTR inputs.

Backends:
  * ``SerialGpioBoard`` - small MCU/relay board driven over a serial line
                          (USB-serial GPIO expander / Arduino-style board).
                          Wire protocol is line-delimited JSON, one command per
                          line:  {"pin": "estop", "action": "press"} etc.
  * ``NullGpioBoard``   - records intended actions and raises on hardware use;
                          used for self-tests and dry-runs (no hardware).
  * ``manual``           - every action is logged as a *request* so a human can
                          execute it and acknowledge (see ``--interactive``).
"""
from __future__ import annotations

import json
import logging
import threading
import time
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import Optional

log = logging.getLogger("system-test.gpio")


@dataclass
class GpioAction:
    pin: str
    action: str  # press | release | pulse | set
    value: Optional[bool] = None
    held_ms: int = 0
    ts: float = 0.0

    def to_dict(self) -> dict:
        return {
            "pin": self.pin,
            "action": self.action,
            "value": self.value,
            "held_ms": self.held_ms,
            "ts": round(self.ts, 3),
        }


class GpioBoard(ABC):
    pins: tuple[str, ...] = ()

    @abstractmethod
    def write(self, action: GpioAction) -> None: ...

    @abstractmethod
    def close(self) -> None: ...

    def press(self, pin: str) -> None:
        self.write(GpioAction(pin=pin, action="press", value=True))

    def release(self, pin: str) -> None:
        self.write(GpioAction(pin=pin, action="release", value=False))

    def pulse(self, pin: str, held_ms: int = 100) -> None:
        """Press, hold, release — used for START / MODE buttons."""
        self.write(GpioAction(pin=pin, action="pulse", held_ms=held_ms))


class NullGpioBoard(GpioBoard):
    """No GPIO hardware. Records actions and fails on physical actuation."""

    def __init__(self) -> None:
        self.history: list[GpioAction] = []
        self._allowed = False

    def enable_recording(self) -> None:
        self._allowed = True

    def write(self, action: GpioAction) -> None:
        action.ts = time.perf_counter()
        self.history.append(action)
        if not self._allowed:
            raise RuntimeError(
                f"no GPIO hardware: cannot {action.action} pin {action.pin!r} "
                "(attach a GPIO board or run with a bench that provides one)"
            )

    def close(self) -> None: ...


class SerialGpioBoard(GpioBoard):
    """USB-serial GPIO/relay board speaking newline-delimited JSON."""

    def __init__(self, port: str, pins: tuple[str, ...], baud: int = 115200) -> None:
        import serial

        self._ser = serial.Serial(port=port, baudrate=baud, timeout=0.5)
        self.pins = pins
        self._lock = threading.Lock()
        self.history: list[GpioAction] = []

    def write(self, action: GpioAction) -> None:
        action.ts = time.perf_counter()
        payload = json.dumps(action.to_dict(), separators=(",", ":")) + "\n"
        with self._lock:
            self._ser.write(payload.encode("ascii"))
            self.history.append(action)
        if action.action == "pulse" and action.held_ms:
            time.sleep(action.held_ms / 1000.0)
            self.write(GpioAction(pin=action.pin, action="release", value=False))

    def close(self) -> None:
        try:
            self._ser.close()
        except Exception:
            pass


class BenchIo:
    """Aggregates the operator inputs a distributed-ESTOP test needs."""

    def __init__(self, board: GpioBoard, clock) -> None:
        self._board = board
        self._clock = clock

    @property
    def board(self) -> GpioBoard:
        return self._board

    def estop_press(self) -> None:
        self._board.write(GpioAction(pin="estop", action="press", value=True))

    def estop_release(self) -> None:
        self._board.write(GpioAction(pin="estop", action="release", value=False))

    def start_press(self, held_ms: int = 100) -> None:
        self._board.pulse("start", held_ms=held_ms)

    def mode_long_press(self, held_ms: int = 3100) -> None:
        self._board.pulse("mode", held_ms=held_ms)

    def mode_short_press(self, held_ms: int = 100) -> None:
        self._board.pulse("mode", held_ms=held_ms)

    def action_log(self) -> list[dict]:
        return [a.to_dict() for a in self._board.history]
