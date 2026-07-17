"""Request/response models for Synthetic Peers API."""

from pydantic import BaseModel, Field


class ListenWindowRequest(BaseModel):
    """Request to start listen window."""

    dut: str = Field(..., description="DUT kind: 'sys' or 'rt'")
    duration_ms: int = Field(default=500, ge=100, le=5000, description="Window duration in ms")


class PeerInfo(BaseModel):
    """Info about a synthetic peer template."""

    name: str = Field(..., description="Peer name (e.g., 'sys:rt_heartbeat_high')")
    key: str = Field(..., description="Message key (e.g., 'rt:rt_heartbeat')")
    bus: str = Field(..., description="Bus name ('high' or 'low')")
    period_ms: int = Field(..., description="Period in milliseconds")
    has_counter: bool = Field(..., description="Whether peer has counter field")


class AvailablePeersResponse(BaseModel):
    """Response with available peer templates."""

    dut: str
    peer_count: int
    peers: list[PeerInfo]


class ListenWindowResponse(BaseModel):
    """Response to listen window start."""

    listening: bool
    dut: str
    duration_ms: int
    remaining_ms: int


class ActivatePeersRequest(BaseModel):
    """Request to activate synthetic peers."""

    dut: str = Field(..., description="DUT kind: 'sys' or 'rt'")


class ActivePeerInstance(BaseModel):
    """Info about an active synthetic peer."""

    name: str
    job_id: str
    key: str
    bus: str
    period_ms: int
    submissions: int = 0


class ActivatePeersResponse(BaseModel):
    """Response to peer activation."""

    activated_count: int
    peers: list[ActivePeerInstance]


class PeerStatusDetail(BaseModel):
    """Detailed status of an active peer."""

    name: str
    key: str
    bus: str
    period_ms: int
    submissions: int


class ConflictDetail(BaseModel):
    """Info about a source conflict."""

    timestamp_ns: int
    bus: str
    can_id: int
    peer: str
    source: str


class SyntheticPeersStatusResponse(BaseModel):
    """Full status of synthetic peers."""

    dut: str | None
    listening: bool
    listen_remaining_ms: int | None
    active_peer_count: int
    active_peers: list[PeerStatusDetail]
    conflicts: list[ConflictDetail]


class StopPeersResponse(BaseModel):
    """Response to stop all peers."""

    stopped_count: int
    status: str


class ConflictReportResponse(BaseModel):
    """Conflict history report."""

    conflict_count: int
    recent_conflicts: list[ConflictDetail]
