"""Node-level faults for nodes the bench owns (and, where noted, ECU power).

``silent`` and ``freeze`` are applied to a restbus node through its boundary
fault mode; a *real* ECU is made silent by power control or bus disconnect, which
is the only honest way to "drop" its frames on a passive bench.

Helper naming follows the 140-item qualification list so a scenario can say
``node_faults.silence(seb)`` and read intent directly.
"""
from __future__ import annotations

from restbus.base import FAULT_FROZEN, FAULT_SILENT, RestbusNode


def silence(node: RestbusNode) -> None:
    """Stop the node transmitting entirely (as if it had been unplugged)."""
    node.fault_mode = FAULT_SILENT


def restore(node: RestbusNode) -> None:
    node.fault_mode = "healthy"


def freeze(node: RestbusNode) -> None:
    """Keep transmitting but never advance counters (frozen ECU image)."""
    node.fault_mode = FAULT_FROZEN


def reboot(node: RestbusNode) -> None:
    """Simulate a reboot by silently cycling the node (drop → healthy fresh epoch)."""
    node.fault_mode = FAULT_SILENT
    node.stop()
    node.start()
    node.fault_mode = "healthy"


def drop_frames(node: RestbusNode, can_id: int, dropped: bool = True) -> None:
    """Drop every frame of one CAN id from that node (heartbeat loss, etc.)."""
    node.drop(can_id, dropped=dropped)
