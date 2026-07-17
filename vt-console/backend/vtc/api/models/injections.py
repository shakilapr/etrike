"""Request/response models for Injection API."""

from datetime import datetime
from enum import Enum

from pydantic import BaseModel, Field


class InjectionStatus(str, Enum):
    """Status of an injection."""

    PENDING = "pending"
    SUBMITTED = "submitted"
    FAILED = "failed"
    CANCELLED = "cancelled"


class SubmitInjectionRequest(BaseModel):
    """Request to submit an injection."""

    key: str = Field(..., description="Message key (e.g., 'host:host_drive_cmd')")
    values: dict = Field(..., description="Engineering values for the message")
    bus: str = Field(..., description="Bus name ('high' or 'low')")
    delay_ms: int = Field(default=0, ge=0, le=60000, description="Delay before sending (ms)")


class InjectionInfo(BaseModel):
    """Info about an injection."""

    injection_id: str
    key: str
    bus: str
    status: str
    can_id: int | None = None
    data: bytes | None = None
    submitted_at: datetime | None = None
    error_code: str | None = None
    error_message: str | None = None
    created_at: datetime


class SubmitInjectionResponse(BaseModel):
    """Response to injection submission."""

    injection_id: str
    status: str
    can_id: int | None = None
    error_code: str | None = None
    error_message: str | None = None


class InjectionListResponse(BaseModel):
    """Response with list of injections."""

    total_count: int
    injections: list[InjectionInfo]


class InjectionDetailResponse(BaseModel):
    """Response with injection details."""

    injection: InjectionInfo


class CancelInjectionResponse(BaseModel):
    """Response to cancel injection."""

    injection_id: str
    previous_status: str
    new_status: str = InjectionStatus.CANCELLED
