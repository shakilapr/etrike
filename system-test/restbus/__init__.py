"""Restbus rig: starts/stops the PC-owned peer nodes and wires their inputs.

Only Host / SES / SEB are ever simulated — the ECU side is real hardware (or a
future vECU) on the same link.
"""
from __future__ import annotations

from typing import Optional

from bench.can import CanLink
from bench.capture import Capture
from restbus.base import RestbusNode
from restbus.host import HostNode
from restbus.seb import SebNode
from restbus.ses import SesNode


class Restbus:
    def __init__(self, link: CanLink, capture: Optional[Capture] = None):
        self.host = HostNode(link)
        self.seb = SebNode(link)
        self.ses = SesNode(link)
        if capture is not None:
            self._wire_from_capture(capture)
        self._capture = capture
        self.nodes: list[RestbusNode] = [self.host, self.seb, self.ses]

    def _wire_from_capture(self, capture: Capture) -> None:
        def seb_cmd():
            sample = capture.latest_on("low", "seb:vcu_seb_req")
            return sample.values if sample and sample.is_valid else None

        def ses_cmd():
            sample = capture.latest_on("low", "ses:vcu_ses_req")
            return sample.values if sample and sample.is_valid else None

        self.seb.set_command_provider(seb_cmd)
        self.ses.set_command_provider(ses_cmd)

    def start(self) -> None:
        for node in self.nodes:
            node.start()

    def stop(self) -> None:
        for node in reversed(self.nodes):
            node.stop()

    def reset(self) -> None:
        for node in self.nodes:
            node.fault_mode = "healthy"
            node.dropped_ids.clear()
            node.corrupt.clear()
