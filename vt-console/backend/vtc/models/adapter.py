"""Adapter status and capability models (workplan §1.2 / §2.3).

Capability fields are tri-state: True / False / None (Unknown). The CANalyst-II
backend cannot report TEC/REC, bus-off, or TX echo — those stay Unknown and are
NEVER surfaced as zero (can-analyzer-research §10).
"""

from __future__ import annotations

from enum import Enum

from pydantic import BaseModel, Field


class AdapterHealth(str, Enum):
    ABSENT = "absent"
    OPENING = "opening"
    OPEN = "open"
    ACTIVE = "active"
    QUIET = "quiet"
    DEGRADED = "degraded"
    RECOVERING = "recovering"
    CLOSED = "closed"


class ChannelActivity(str, Enum):
    UNSEEN = "unseen"
    ACTIVE = "active"
    QUIET = "quiet"


class Capability(BaseModel):
    """Tri-state adapter capabilities; None means Unknown (never assume)."""

    model_config = {"frozen": True}

    hw_timestamps: bool | None = None
    tx_echo: bool | None = None
    listen_only: bool | None = None
    bus_off_reporting: bool | None = None
    tec_rec_reporting: bool | None = None


class ChannelState(BaseModel):
    channel: str
    activity: ChannelActivity = ChannelActivity.UNSEEN
    last_rx_ns: int | None = None
    rx_count: int = 0
    tx_count: int = 0
    rx_overflow: int = 0
    queue_high_water: int = 0


class AdapterStatus(BaseModel):
    """Full adapter status snapshot."""

    identity: str = "none"
    health: AdapterHealth = AdapterHealth.ABSENT
    adapter_epoch: int = 0
    capability: Capability = Field(default_factory=Capability)
    channels: dict[str, ChannelState] = Field(default_factory=dict)
