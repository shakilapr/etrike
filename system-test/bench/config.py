"""L9 system-bench configuration.

Canonical hardware wiring (matches control-toolkit transport + docs):
  CANalyst-II CH0  -> High CAN  (RT <-> Host / control PC)
  CANalyst-II CH1  -> Low  CAN  (RT / SYS / MTR / SES / SEB)
  500 kbit/s both buses.

NOTE: docs/testing_and_validation/can-bench-test.md and can-test/ still describe
the legacy reversed mapping (CH0 = Low). The reversed mapping is STALE; the code
and current hardware docs use CH0 = High / CH1 = Low.
"""
from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path

BITRATE = 500_000

HIGH_CHANNEL = 0
LOW_CHANNEL = 1


@dataclass
class BenchConfig:
    transport: str = "auto"  # auto | canalystii | loopback
    traces_root: Path = field(default_factory=lambda: Path("traces"))
    bitrate: int = BITRATE
    device: int = 0
    channel_high: int = HIGH_CHANNEL
    channel_low: int = LOW_CHANNEL
    poll_ms: float = 2.0
    # python-can virtual bus channel names used by the loopback (self-test) link.
    loopback_high: str = field(default_factory=lambda: _env("SYSTEMTEST_LOOPBACK_HIGH", "etrike_sysbench_high"))
    loopback_low: str = field(default_factory=lambda: _env("SYSTEMTEST_LOOPBACK_LOW", "etrike_sysbench_low"))

    def physical_channel_for(self, bus: str) -> int:
        if bus == "high":
            return self.channel_high
        if bus == "low":
            return self.channel_low
        raise ValueError(f"unknown logical bus: {bus!r}")

    def bus_for_physical_channel(self, channel: int) -> str:
        if channel == self.channel_high:
            return "high"
        if channel == self.channel_low:
            return "low"
        raise ValueError(f"unknown physical channel: {channel!r}")

    def loopback_channel(self, bus: str) -> str:
        if bus == "high":
            return self.loopback_high
        if bus == "low":
            return self.loopback_low
        raise ValueError(f"unknown logical bus: {bus!r}")


def _env(name: str, default: str) -> str:
    return os.environ.get(name, default)
