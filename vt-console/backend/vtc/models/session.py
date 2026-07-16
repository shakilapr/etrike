"""Session, profile, Bench TX, and lease models (workplan §3).

Phase 1 shipped a stub ``SessionState`` so the status snapshot was stable early.
Phase 3 fills it out: session identity, revision for concurrent mutation
control, and the request bodies for the session API (§3.5).
"""

from __future__ import annotations

from enum import Enum

from pydantic import BaseModel, Field

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
    """Full session snapshot for the API and status endpoint."""

    profile: Profile = Profile.PURE_SOFTWARE
    phase: SessionPhase = SessionPhase.STOPPED
    bench_tx: BenchTxState = BenchTxState.DISABLED
    session_id: str | None = None
    test_session_id: str | None = None
    revision: int = 0
    adapter_epoch: int | None = None
    wire_hash: str | None = None
    destination: str = "virtual"  # virtual | physical
    capabilities: list[str] = Field(default_factory=list)
    leases: list[str] = Field(default_factory=list)


class CreateSessionRequest(BaseModel):
    profile: Profile = Profile.PURE_SOFTWARE
    capabilities: list[str] = Field(default_factory=lambda: ["observe", "inject"])
    test_session_id: str | None = None


class ChangeProfileRequest(BaseModel):
    profile: Profile
    expected_revision: int | None = None
    confirm: bool = False  # must be true — controlled transition, no accidental switch


class BenchTxRequest(BaseModel):
    enabled: bool
    expected_revision: int | None = None


class StopAllRequest(BaseModel):
    expected_revision: int | None = None


class CloseSessionRequest(BaseModel):
    expected_revision: int | None = None
    outcome: SessionPhase = SessionPhase.STOPPED  # completed/failed/inconclusive/stopped


class ClaimLeaseRequest(BaseModel):
    bus: str
    can_id: int
    owner: str
    resource: str | None = None
    ttl_s: float = 5.0


class RenewLeaseRequest(BaseModel):
    lease_id: str
    ttl_s: float = 5.0
