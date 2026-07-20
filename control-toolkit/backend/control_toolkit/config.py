"""Typed backend configuration with defaults from architecture §4.5 / §18.1.

Single-process, single-worker Uvicorn is a hard requirement: the receive
pipeline, latest-state store, and scheduler assume one owner. Values here are
overridable via ``CTK_*`` environment variables through :meth:`ToolkitConfig.from_env`.
"""

from __future__ import annotations

import os
from enum import Enum

from pydantic import BaseModel, Field


class Profile(str, Enum):
    """Operating profile (architecture §3). Default is the hardware-free profile."""

    FULL_VEHICLE = "full_vehicle"
    BENCH_TEST = "bench_test"
    PURE_SOFTWARE = "pure_software"


class ToolkitConfig(BaseModel):
    """Immutable backend configuration snapshot."""

    model_config = {"frozen": True}

    host: str = "127.0.0.1"
    port: int = 8000
    # Single worker is mandatory — do not raise (architecture §4.5).
    workers: int = 1

    default_profile: Profile = Profile.PURE_SOFTWARE

    # Stream/service-level targets (architecture §18.1), milliseconds.
    stream_heartbeat_ms: int = 250
    latest_state_batch_hz: int = 25
    browser_degraded_ms: int = 750
    browser_lost_ms: int = 1500

    # RX queue bound; overflow is counted and surfaced, never silently evicted.
    rx_queue_maxsize: int = 65536
    # Chronological frame history capacity (bounded ring).
    history_capacity: int = 4096

    # Optional native software-in-the-loop peer. When set, Pure Software sends
    # HOST_DRIVE_CMD frames to this JSON-Lines process and receives its CAN
    # responses through the normal virtual transport/router path.
    native_sil_executable: str | None = None

    # CANalyst-II physical transport.  Keep these explicit rather than relying
    # on python-can global configuration so the Settings API and bench evidence
    # describe the exact hardware setup in use.
    canalyst_device_index: int = Field(default=0, ge=0)
    canalyst_bitrate: int = Field(default=500_000, gt=0)
    canalyst_poll_ms: float = Field(default=2.0, ge=1.0, le=100.0)
    canalyst_receive_timeout_ms: float = Field(default=100.0, ge=10.0, le=1000.0)
    canalyst_reconnect_initial_ms: float = Field(default=250.0, ge=10.0)
    canalyst_reconnect_max_ms: float = Field(default=5_000.0, ge=10.0)
    canalyst_recovery_stability_ms: float = Field(default=500.0, ge=0.0)

    title: str = "E-Trike Control Toolkit"
    api_prefix: str = "/api/v1"

    @classmethod
    def from_env(cls) -> "ToolkitConfig":
        """Build config from ``CTK_*`` environment variables, falling back to defaults."""
        overrides: dict[str, object] = {}
        if v := os.getenv("CTK_HOST"):
            overrides["host"] = v
        if v := os.getenv("CTK_PORT"):
            overrides["port"] = int(v)
        if v := os.getenv("CTK_PROFILE"):
            overrides["default_profile"] = Profile(v)
        if v := os.getenv("CTK_NATIVE_SIL_EXE"):
            overrides["native_sil_executable"] = v
        if v := os.getenv("CTK_CANALYST_DEVICE_INDEX"):
            overrides["canalyst_device_index"] = int(v)
        if v := os.getenv("CTK_CANALYST_BITRATE"):
            overrides["canalyst_bitrate"] = int(v)
        if v := os.getenv("CTK_CANALYST_POLL_MS"):
            overrides["canalyst_poll_ms"] = float(v)
        if v := os.getenv("CTK_CANALYST_RECEIVE_TIMEOUT_MS"):
            overrides["canalyst_receive_timeout_ms"] = float(v)
        if v := os.getenv("CTK_CANALYST_RECONNECT_INITIAL_MS"):
            overrides["canalyst_reconnect_initial_ms"] = float(v)
        if v := os.getenv("CTK_CANALYST_RECONNECT_MAX_MS"):
            overrides["canalyst_reconnect_max_ms"] = float(v)
        if v := os.getenv("CTK_CANALYST_RECOVERY_STABILITY_MS"):
            overrides["canalyst_recovery_stability_ms"] = float(v)
        return cls(**overrides)
