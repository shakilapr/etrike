"""Session and profile state models (workplan §3, stubbed in Phase 1).

Filled out in Phase 3 (profiles, session state machine, Bench TX, leases).
Defined here so the API surface and status snapshot are stable from Phase 1.
"""

from __future__ import annotations

from enum import Enum

from pydantic import BaseModel

from vtc.config import Profile


class SessionPhase(str, Enum):
    STOPPED = "stopped"
    PREPARING = "preparing"
    LISTENING = "listening"
    RUNNING = "running"
    STOPPING = "stopping"
    COMPLETED = "completed"
    FAILED = "failed"
    INCONCLUSIVE = "inconclusive"


class BenchTxState(str, Enum):
    """Bench TX is Disabled until an explicit operator action (workplan §3.3)."""

    DISABLED = "disabled"
    ENABLED = "enabled"


class SessionState(BaseModel):
    """Current session snapshot. Phase 1 always reports a stopped session."""

    profile: Profile = Profile.PURE_SOFTWARE
    phase: SessionPhase = SessionPhase.STOPPED
    bench_tx: BenchTxState = BenchTxState.DISABLED
    session_id: str | None = None
    revision: int = 0
