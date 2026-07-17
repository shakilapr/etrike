"""Request/response models for HMI API."""

from datetime import datetime
from enum import Enum

from pydantic import BaseModel, Field


class Profile(str, Enum):
    """Operating profile."""

    PURE_SOFTWARE = "pure_software"
    BENCH_TEST = "bench_test"
    FULL_VEHICLE = "full_vehicle"


class BenchTxState(str, Enum):
    """Bench TX state."""

    DISABLED = "disabled"
    ENABLED = "enabled"


class DutKind(str, Enum):
    """Device-under-test kind."""

    SYS = "sys"
    RT = "rt"


class CreateSessionRequest(BaseModel):
    """Request to create a session."""

    name: str = Field(..., description="Session name (user-friendly)")
    profile: str = Field(default="pure_software", description="Operating profile")
    dut: str = Field(..., description="Device-under-test ('sys' or 'rt')")


class SessionInfo(BaseModel):
    """Info about a session."""

    session_id: str
    name: str
    profile: str
    dut: str
    bench_tx_enabled: bool
    created_at: datetime
    updated_at: datetime


class CreateSessionResponse(BaseModel):
    """Response to session creation."""

    session_id: str
    name: str
    profile: str
    dut: str
    created_at: datetime


class SessionListResponse(BaseModel):
    """Response with list of sessions."""

    total_count: int
    sessions: list[SessionInfo]


class SessionDetailResponse(BaseModel):
    """Response with session details."""

    session: SessionInfo


class UpdateSessionRequest(BaseModel):
    """Request to update session."""

    profile: str | None = None
    bench_tx_enabled: bool | None = None


class UpdateSessionResponse(BaseModel):
    """Response to session update."""

    session_id: str
    profile: str | None = None
    bench_tx_enabled: bool | None = None


class SystemStatusResponse(BaseModel):
    """Overall system status."""

    session_id: str
    profile: str
    dut: str
    bench_tx_enabled: bool
    synthetic_peers_active: int
    pending_injections: int
    submitted_injections: int
    total_conflicts: int
    listening: bool


class ControlWorkspaceStatusResponse(BaseModel):
    """Status for control workspace UI."""

    session_id: str
    profile: str
    dut: str
    bench_tx_enabled: bool
    synthetic_peers: dict
    injections: dict
    system_status: str


class StartBenchTestRequest(BaseModel):
    """Request to start bench testing."""

    dut: str = Field(..., description="Device-under-test ('sys' or 'rt')")
    listen_duration_ms: int = Field(default=500, ge=100, le=5000)


class StartBenchTestResponse(BaseModel):
    """Response to bench test start."""

    session_id: str
    status: str
    listening: bool
    listening_remaining_ms: int


class StopBenchTestResponse(BaseModel):
    """Response to bench test stop."""

    session_id: str
    status: str
    synthetic_peers_stopped: int
    pending_injections_cancelled: int


class DeleteSessionResponse(BaseModel):
    """Response to session deletion."""

    session_id: str
    status: str
    cleaned_up_peers: int
    cleaned_up_injections: int
