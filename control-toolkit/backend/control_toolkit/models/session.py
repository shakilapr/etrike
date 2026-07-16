"""Session, profile, Bench TX, and lease models."""

from __future__ import annotations

from enum import Enum

from pydantic import BaseModel, Field

from control_toolkit.config import Profile


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
    """Bench TX is Disabled until an explicit operator action."""

    DISABLED = "disabled"
    ENABLED = "enabled"


class SessionState(BaseModel):
    """Current session snapshot returned by the API."""

    profile: Profile = Profile.PURE_SOFTWARE
    phase: SessionPhase = SessionPhase.STOPPED
    bench_tx: BenchTxState = BenchTxState.DISABLED
    session_id: str | None = None
    revision: int = 0
    adapter_epoch: int | None = None
    wire_hash: str | None = None
    leases: list[str] = Field(default_factory=list)
    jobs: list[str] = Field(default_factory=list)


class CreateSessionRequest(BaseModel):
    profile: Profile = Profile.PURE_SOFTWARE
    capabilities: list[str] = Field(default_factory=lambda: ["observe", "inject"])


class BenchTxRequest(BaseModel):
    enabled: bool
    expected_revision: int | None = None


class StopAllRequest(BaseModel):
    expected_revision: int | None = None


class CloseSessionRequest(BaseModel):
    expected_revision: int | None = None
