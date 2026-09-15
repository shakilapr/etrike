"""Directive guard: the bench suite must never fake absent actuators.

This test needs no hardware — it statically inspects the suite's own sources
and always runs, so a future edit that reintroduces synthetic SES/SEB/MTR
feedback fails loudly in CI.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from harness import FORBIDDEN_MOCK_KEYS

pytestmark = pytest.mark.hw_bench

HERE = Path(__file__).resolve().parent


def _suite_sources():
    for path in sorted(HERE.glob("test_*.py")):
        if path.name == Path(__file__).name:
            continue  # this file only imports the token list
        yield path, path.read_text(encoding="utf-8")


def test_no_absent_actuator_feedback_is_injected():
    """No suite source may reference absent-actuator feedback/status keys."""
    offenders = []
    for path, text in _suite_sources():
        for token in FORBIDDEN_MOCK_KEYS:
            if token in text:
                offenders.append(f"{path.name}: {token}")
    assert not offenders, (
        "hardware bench directive violated — synthetic actuator feedback keys found: "
        + ", ".join(offenders)
    )


def test_suite_never_opens_the_can_adapter_directly():
    """The backend owns the CANalyst-II; the suite must not open can.Bus."""
    offenders = []
    for path, text in _suite_sources():
        if "can.Bus(" in text or 'interface="canalystii"' in text or "interface='canalystii'" in text:
            offenders.append(path.name)
    assert not offenders, (
        "suite opens the CAN adapter directly; only the backend may own it: "
        + ", ".join(offenders)
    )
