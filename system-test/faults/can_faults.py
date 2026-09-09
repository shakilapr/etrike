"""Fault-injection at external boundaries.

Every fault below is introduced by changing *external behaviour* — frames a node
sends, silence, reboots, corruption — never internal ECU state. Logical faults
on real-ECU streams (dropping SYS 0x011 etc.) are only possible on a bench that
owns the bus (a second CANalyst / gateway); the API documents that dependency so
a scenario states its precondition instead of pretending.
"""
from __future__ import annotations

from typing import Callable, Optional


class FrameMutations:
    """Reusable byte-level mutations applied to frames before transmission."""

    @staticmethod
    def xor_byte(index: int, mask: int) -> Callable[[bytes], bytes]:
        def mutate(data: bytes) -> bytes:
            if index >= len(data):
                return data
            raw = bytearray(data)
            raw[index] ^= mask
            return bytes(raw)

        return mutate

    @staticmethod
    def overwrite(index: int, value: int) -> Callable[[bytes], bytes]:
        def mutate(data: bytes) -> bytes:
            if index >= len(data):
                return data
            raw = bytearray(data)
            raw[index] = value & 0xFF
            return bytes(raw)

        return mutate

    @staticmethod
    def truncate(new_length: int) -> Callable[[bytes], bytes]:
        def mutate(data: bytes) -> bytes:
            return bytes(data[:new_length])

        return mutate
