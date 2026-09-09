"""Actuator restbus fault switches (SEB / SES behavioural faults).

These are aliases to the vendor-report fault modes implemented in the restbus
nodes so scenario text reads like the test catalogue:
  stuck  -> actuator refuses to follow the commanded value
  silent -> actuator goes offline
  slow   -> actuator lags the command
  l3     -> actuator reports a latched L3 fault
"""
from __future__ import annotations

from typing import Union

from restbus.seb import SebNode
from restbus.ses import SesNode

Actuator = Union[SebNode, SesNode]

SEB_MODES = {"healthy", "silent", "frozen", "stuck", "slow", "l3_fault"}
SES_MODES = {"healthy", "silent", "frozen", "stuck", "slow", "l3_fault"}


def seb_set(seb: SebNode, mode: str) -> None:
    if mode not in SEB_MODES:
        raise ValueError(f"unsupported SEB fault mode {mode!r}")
    seb.inject_fault(mode)


def ses_set(ses: SesNode, mode: str) -> None:
    if mode not in SES_MODES:
        raise ValueError(f"unsupported SES fault mode {mode!r}")
    ses.inject_fault(mode)
