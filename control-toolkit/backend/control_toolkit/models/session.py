"""Session, profile, Bench TX, and lease models (Phase 3)."""

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
    DISABLED = "disabled"
    ENABLED = "enabled"


class SessionState(BaseModel):
    """Full session snapshot for API and WebSocket."""

    profile: Profile = Profile.PURE_SOFTWARE
    phase: SessionPhase = SessionPhase.STOPPED
    bench_tx: BenchTxState = BenchTxState.DISABLED
    session_id: str | None = None
    test_session_id: str | None = None
    revision: int = 0
    adapter_epoch: int | None = None
    wire_hash: str | None = None
    semantic_hash: str | None = None
    destination: str = "virtual"  # virtual | physical
    capabilities: list[str] = Field(default_factory=list)
    leases: list[str] = Field(default_factory=list)
    jobs: list[str] = Field(default_factory=list)
    # Requested vs observed vehicle state (UI header; not firmware authority).
    requested_mode: str | None = None  # MANUAL | AUTO | PURE_SIM
    confirmed_mode: str | None = None
    requested_power: str | None = None  # OFF | ON
    confirmed_power: str | None = None
    estop_active: bool | None = None
    recording: bool = False


class CreateSessionRequest(BaseModel):
    profile: Profile = Profile.PURE_SOFTWARE
    capabilities: list[str] = Field(default_factory=lambda: ["observe", "inject"])
    test_session_id: str | None = None


class ChangeProfileRequest(BaseModel):
    profile: Profile
    expected_revision: int | None = None
    confirm: bool = False  # must be true for controlled transition


class BenchTxRequest(BaseModel):
    enabled: bool
    expected_revision: int | None = None


class StopAllRequest(BaseModel):
    expected_revision: int | None = None


class CloseSessionRequest(BaseModel):
    expected_revision: int | None = None
    outcome: SessionPhase = SessionPhase.STOPPED  # completed/failed/inconclusive/stopped


class RenewLeaseRequest(BaseModel):
    lease_id: str
    ttl_s: float = 5.0


class ClaimLeaseRequest(BaseModel):
    bus: str
    can_id: int
    owner: str
    resource: str | None = None
    ttl_s: float = 5.0


class VehicleViewRequest(BaseModel):
    """Operator-facing requested/observed vehicle state (not firmware authority)."""

    requested_mode: str | None = None  # MANUAL | AUTO | PURE_SIM
    confirmed_mode: str | None = None
    requested_power: str | None = None  # OFF | ON
    confirmed_power: str | None = None
    estop_active: bool | None = None
    recording: bool | None = None
