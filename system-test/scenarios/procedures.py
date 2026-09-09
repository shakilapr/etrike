"""Reusable external-boundary steps shared by L9 scenarios.

Every step drives the bench through the *real* interfaces: frames the bench
owns, physical GPIO inputs, ECU power. Steps never reach inside an ECU.
"""
from __future__ import annotations

from assertions.temporal import wait_for_valid
from bench.session import BenchSession
from bench.wire import (
    LOW,
    RT_HEARTBEAT,
    SYS_HEARTBEAT,
    SYS_SAFETY_STS,
    safety_sts_e2e_ok,
)

NODE_TIMEOUT_S = 10.0


def wait_all_nodes_healthy(bench: BenchSession, timeout: float = NODE_TIMEOUT_S) -> None:
    """Wait until RT/SYS/MTR are all publishing (SES/SEB restbus already own)."""
    capture = bench.capture
    wait_for_valid(capture, LOW, RT_HEARTBEAT, timeout=timeout)
    wait_for_valid(capture, LOW, SYS_HEARTBEAT, timeout=timeout)
    wait_for_valid(capture, LOW, SYS_SAFETY_STS, timeout=timeout)


def wait_sys_clear(bench: BenchSession, timeout: float = 5.0) -> None:
    """Wait until SYS 0x011 estop_active == 0 with a valid E2E CRC."""

    def match(sample):
        return (
            sample.bus == LOW
            and sample.key == SYS_SAFETY_STS
            and sample.is_valid
            and sample.values["estop_active"] == 0
            and safety_sts_e2e_ok(sample.data)
        )

    bench.capture.wait_for(match, timeout=timeout)


def count_valid_clear_frames(bench: BenchSession) -> int:
    """Number of consecutive advancing E2E-valid 0x011 clear frames seen so far."""
    seen: list[int] = []
    for sample in bench.capture.all():
        if sample.bus == LOW and sample.key == SYS_SAFETY_STS and sample.is_valid:
            if sample.values["estop_active"] == 0 and safety_sts_e2e_ok(sample.data):
                seen.append(int(sample.values["rolling_counter"]))
    if not seen:
        return 0
    runs, prev = 1, seen[0]
    for value in seen[1:]:
        if (value - prev) % 256 == 1:
            runs += 1
        prev = value
    return runs


def operator_start_reset(bench: BenchSession) -> None:
    """Press physical START and wait for the SYS latch to clear."""
    bench.io.start_press(held_ms=100)
    wait_sys_clear(bench)
