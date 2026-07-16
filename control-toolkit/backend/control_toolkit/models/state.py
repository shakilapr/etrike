"""Latest-value and freshness state models (workplan §1.5).

Freshness ages on its own clock — a message becomes Late/Missing with no new
frames arriving. States are never faked to zero; unknown stays unknown.
"""

from __future__ import annotations

from enum import Enum

from pydantic import BaseModel, Field


class FreshnessState(str, Enum):
    UNSEEN = "unseen"
    LIVE = "live"
    LATE = "late"
    MISSING = "missing"
    INVALID = "invalid"
    FROZEN = "frozen"
    RECOVERING = "recovering"


class SignalValue(BaseModel):
    model_config = {"frozen": True}

    raw_value: int | float | None = None
    engineering_value: int | float | str | None = None
    unit: str | None = None
    enum_label: str | None = None
    valid: bool = True


class MessageState(BaseModel):
    """Latest observation for one ``(bus, can_id)`` runtime identity."""

    bus: str
    can_id: int
    key: str | None = None
    name: str | None = None
    last_seen_ns: int | None = None
    observed_rate_hz: float | None = None
    expected_rate_hz: float | None = None
    freshness: FreshnessState = FreshnessState.UNSEEN
    validation_status: str | None = None
    signals: dict[str, SignalValue] = Field(default_factory=dict)


class LatestStateSnapshot(BaseModel):
    """Atomic snapshot of latest state with a monotonic sequence for gap detection."""

    sequence: int
    wire_hash: str
    messages: list[MessageState] = Field(default_factory=list)
