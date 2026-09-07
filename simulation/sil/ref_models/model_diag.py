"""Pure-Python Reference Model for etrike::diagnostics::DiagnosticManager.

Implements the exact Phase-B diagnostic state machine and lifecycle contract:
CLEARED, ACTIVE, RECOVERED, LATCHED, ESTOP episode origin tracking, and report queuing.
Used for differential testing against the C++ DiagnosticManager implementation.
"""

from dataclasses import dataclass
from enum import IntEnum
from typing import Dict, List, Optional, Tuple


class DiagState(IntEnum):
    PENDING = 0
    ACTIVE = 1
    LATCHED = 2
    RECOVERED = 3
    CLEARED = 4


@dataclass(frozen=True)
class DiagMeta:
    diag_id: int
    name: str
    latching: bool
    is_estop_cause: bool


@dataclass
class DiagReport:
    diag_id: int
    state: DiagState
    occurrence_count: int
    report_counter: int
    flags: int
    snapshot_data: int


class PythonDiagnosticManager:
    """Independent reference model for DiagnosticManager."""

    FLAG_FIRST_LOCAL_ESTOP_CAUSE = 0x01

    def __init__(self, metadata_table: List[DiagMeta]):
        self.meta_table = metadata_table
        self.capacity = len(metadata_table)
        self.id_to_index: Dict[int, int] = {m.diag_id: i for i, m in enumerate(metadata_table)}

        # Runtime records per dense index
        self.state: List[DiagState] = [DiagState.CLEARED] * self.capacity
        self.occurrence_count: List[int] = [0] * self.capacity
        self.snapshot: List[int] = [0] * self.capacity
        self.snapshot_supplied: List[bool] = [False] * self.capacity
        self.pending_report: List[bool] = [False] * self.capacity
        self.reset_count_after_drain: List[bool] = [False] * self.capacity

        self.report_counter = 0
        self.estop_episode_claimed = False
        self.first_estop_cause_index: Optional[int] = None
        self.pop_cursor = 0

    def raise_event(self, diag_id: int, snapshot: int = 0, supplied: bool = False) -> None:
        idx = self.id_to_index.get(diag_id)
        if idx is None:
            return

        meta = self.meta_table[idx]
        if self.state[idx] == DiagState.ACTIVE:
            if self.snapshot[idx] != snapshot:
                self.snapshot[idx] = snapshot
                self.snapshot_supplied[idx] = supplied
            return

        self.state[idx] = DiagState.ACTIVE
        if self.occurrence_count[idx] < 255:
            self.occurrence_count[idx] += 1
        self.snapshot[idx] = snapshot
        self.snapshot_supplied[idx] = supplied
        self.pending_report[idx] = True

        if meta.is_estop_cause and not self.estop_episode_claimed:
            self.estop_episode_claimed = True
            self.first_estop_cause_index = idx

    def recover(self, diag_id: int) -> None:
        idx = self.id_to_index.get(diag_id)
        if idx is None:
            return

        if self.state[idx] != DiagState.ACTIVE:
            return

        meta = self.meta_table[idx]
        self.state[idx] = DiagState.LATCHED if meta.latching else DiagState.RECOVERED
        self.pending_report[idx] = True

    def clear(self, diag_id: int) -> None:
        idx = self.id_to_index.get(diag_id)
        if idx is None:
            return

        if self.state[idx] in (DiagState.LATCHED, DiagState.RECOVERED):
            self.state[idx] = DiagState.CLEARED
            self.pending_report[idx] = True
            self.reset_count_after_drain[idx] = True

    def clear_all(self) -> None:
        for idx in range(self.capacity):
            if self.state[idx] in (DiagState.LATCHED, DiagState.RECOVERED):
                self.state[idx] = DiagState.CLEARED
                self.pending_report[idx] = True
                self.reset_count_after_drain[idx] = True

    def on_estop_episode_cleared(self) -> None:
        self.estop_episode_claimed = False
        self.first_estop_cause_index = None

    def pop_pending_report(self) -> Optional[DiagReport]:
        for n in range(self.capacity):
            i = (self.pop_cursor + n) % self.capacity
            if not self.pending_report[i]:
                continue

            self.pending_report[i] = False
            self.pop_cursor = (i + 1) % self.capacity
            meta = self.meta_table[i]

            self.report_counter = (self.report_counter + 1) & 0xFF

            flags = 0
            if self.first_estop_cause_index == i:
                flags |= self.FLAG_FIRST_LOCAL_ESTOP_CAUSE
                self.first_estop_cause_index = None

            report = DiagReport(
                diag_id=meta.diag_id,
                state=self.state[i],
                occurrence_count=self.occurrence_count[i],
                report_counter=self.report_counter,
                flags=flags,
                snapshot_data=self.snapshot[i],
            )

            if self.state[i] == DiagState.CLEARED and self.reset_count_after_drain[i]:
                self.occurrence_count[i] = 0
                self.reset_count_after_drain[i] = False

            return report
        return None

    def replay_active_set(self) -> None:
        for i in range(self.capacity):
            if self.state[i] in (DiagState.ACTIVE, DiagState.LATCHED):
                self.pending_report[i] = True
