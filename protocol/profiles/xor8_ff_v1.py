"""Versioned XOR(bytes) ^ 0xFF integrity profile."""

from collections.abc import Iterable

PROFILE_ID = "xor8_ff_v1"


def compute(data: Iterable[int]) -> int:
    value = 0
    for byte in data:
        if not 0 <= byte <= 0xFF:
            raise ValueError("profile input bytes must be in range 0..255")
        value ^= byte
    return value ^ 0xFF


def verify(data: Iterable[int], checksum: int) -> bool:
    return 0 <= checksum <= 0xFF and compute(data) == checksum
