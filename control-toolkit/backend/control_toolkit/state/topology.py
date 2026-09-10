"""ECU liveness / topology derived from heartbeat messages.

RT 0x7FD High and Low are independent (never merged). States:
  offline | live | late | missing | fault | unknown
"""

from __future__ import annotations

import threading
import time
from enum import Enum

from pydantic import BaseModel, Field

from control_toolkit.models.state import FreshnessState, MessageState
from control_toolkit.pipeline.freshness import classify


class NodeLiveness(str, Enum):
    OFFLINE = "offline"
    LIVE = "live"
    LATE = "late"
    MISSING = "missing"
    FAULT = "fault"
    UNKNOWN = "unknown"


# Heartbeat / status probes used for topology (bus, can_id, node label).
# SES/SEB use periodic status frames (not pure heartbeats) as presence on Low.
# MTR presence is probed via its NODE_STATUS frame (0x502), not the 0x206 echo,
# so the topology reflects node_state/estop_latched rather than feedback liveness.
HEARTBEAT_NODES: tuple[tuple[str, int, str], ...] = (
    ("high", 0x7FC, "Host"),
    ("high", 0x7FD, "RT_high"),
    ("low", 0x7FD, "RT_low"),
    ("low", 0x7FE, "SYS"),
    ("low", 0x502, "MTR"),
    ("low", 0x201, "SES"),
    ("low", 0x721, "SEB"),
)


class NodeState(BaseModel):
    node: str
    bus: str
    can_id: int
    liveness: NodeLiveness = NodeLiveness.OFFLINE
    last_seen_ns: int | None = None
    freshness: FreshnessState = FreshnessState.UNSEEN
    validation_status: str | None = None


class TopologySnapshot(BaseModel):
    nodes: list[NodeState] = Field(default_factory=list)
    updated_ns: int = 0


class TopologyTracker:
    def __init__(
        self,
        *,
        on_liveness_change: Callable[[NodeState, NodeLiveness, NodeLiveness], None] | None = None,
    ) -> None:
        self._lock = threading.Lock()
        self._nodes: dict[str, NodeState] = {
            label: NodeState(node=label, bus=bus, can_id=can_id)
            for bus, can_id, label in HEARTBEAT_NODES
        }
        self._prev_liveness: dict[str, NodeLiveness] = {
            label: NodeLiveness.OFFLINE for bus, can_id, label in HEARTBEAT_NODES
        }
        self._on_liveness_change = on_liveness_change

    def observe(self, message: MessageState) -> None:
        """Update topology from a latest message observation."""
        changes: list[tuple[NodeState, NodeLiveness, NodeLiveness]] = []
        with self._lock:
            for bus, can_id, label in HEARTBEAT_NODES:
                if message.bus == bus and message.can_id == can_id:
                    node = self._nodes[label]
                    prev = self._prev_liveness.get(label, NodeLiveness.OFFLINE)
                    node.last_seen_ns = message.last_seen_ns
                    node.freshness = message.freshness
                    node.validation_status = message.validation_status
                    node.liveness = self._map_liveness(
                        message.freshness, message.validation_status
                    )
                    if node.liveness != prev:
                        self._prev_liveness[label] = node.liveness
                        changes.append((node.model_copy(), prev, node.liveness))
                    break

        if self._on_liveness_change is not None:
            for node, prev, curr in changes:
                try:
                    self._on_liveness_change(node, prev, curr)
                except Exception:
                    pass

    def reclassify(self, messages: dict[tuple[str, int], MessageState], now_ns: int) -> None:
        changes: list[tuple[NodeState, NodeLiveness, NodeLiveness]] = []
        with self._lock:
            for bus, can_id, label in HEARTBEAT_NODES:
                msg = messages.get((bus, can_id))
                node = self._nodes[label]
                prev = self._prev_liveness.get(label, NodeLiveness.OFFLINE)
                if msg is None:
                    node.liveness = NodeLiveness.OFFLINE
                    node.freshness = FreshnessState.UNSEEN
                    if node.liveness != prev:
                        self._prev_liveness[label] = node.liveness
                        changes.append((node.model_copy(), prev, node.liveness))
                    continue
                cycle_ms = (
                    int(round(1000 / msg.expected_rate_hz))
                    if msg.expected_rate_hz
                    else 0
                )
                fresh = classify(
                    msg.validation_status, msg.last_seen_ns, cycle_ms, now_ns
                )
                node.last_seen_ns = msg.last_seen_ns
                node.freshness = fresh
                node.validation_status = msg.validation_status
                node.liveness = self._map_liveness(fresh, msg.validation_status)
                if node.liveness != prev:
                    self._prev_liveness[label] = node.liveness
                    changes.append((node.model_copy(), prev, node.liveness))

        if self._on_liveness_change is not None:
            for node, prev, curr in changes:
                try:
                    self._on_liveness_change(node, prev, curr)
                except Exception:
                    pass

    def snapshot(self) -> TopologySnapshot:
        with self._lock:
            return TopologySnapshot(
                nodes=[n.model_copy() for n in self._nodes.values()],
                updated_ns=time.monotonic_ns(),
            )

    def clear(self) -> None:
        """Reset node liveness (no observed heartbeats after transport switch)."""
        with self._lock:
            for label, node in self._nodes.items():
                node.liveness = NodeLiveness.OFFLINE
                node.last_seen_ns = None
                node.freshness = FreshnessState.UNSEEN
                node.validation_status = None
                self._prev_liveness[label] = NodeLiveness.OFFLINE

    @staticmethod
    def _map_liveness(
        freshness: FreshnessState, validation: str | None
    ) -> NodeLiveness:
        if freshness is FreshnessState.UNSEEN:
            return NodeLiveness.OFFLINE
        if freshness is FreshnessState.INVALID or (
            validation and validation not in (None, "ok", "unknown_id")
        ):
            return NodeLiveness.FAULT
        if freshness is FreshnessState.LIVE:
            return NodeLiveness.LIVE
        if freshness is FreshnessState.LATE:
            return NodeLiveness.LATE
        if freshness is FreshnessState.MISSING:
            return NodeLiveness.MISSING
        return NodeLiveness.UNKNOWN
