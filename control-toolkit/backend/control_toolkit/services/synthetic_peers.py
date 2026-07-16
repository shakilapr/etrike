"""Optional analysis stimuli — not full multi-ECU synthetic peers.

Pure Software testing assumes firmware safety-bypass (or no ECU). We only
schedule the few frames under study (e.g. HOST_DRIVE_CMD for yaw/speed), not a
full heartbeat mesh for every missing node.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from control_toolkit.models.frames import FrameSource
from control_toolkit.services.scheduler import Scheduler


@dataclass(frozen=True)
class PeerSpec:
    name: str
    bus: str
    key: str
    period_ms: float
    values: dict[str, Any]
    counter_field: str | None = None


# Analysis-only presets (explicit start by name). No full vehicle peer set.
ANALYSIS_STIMULI: tuple[PeerSpec, ...] = (
    PeerSpec(
        "host_drive_analysis",
        "high",
        "host:host_drive_cmd",
        100,
        {"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 1},
        None,
    ),
)

# Back-compat alias for API list.
DEFAULT_PEERS = ANALYSIS_STIMULI


class SyntheticPeerService:
    def __init__(self, scheduler: Scheduler) -> None:
        self._scheduler = scheduler
        self._jobs: dict[str, str] = {}

    def list_running(self) -> list[dict[str, str]]:
        return [{"name": n, "job_id": j} for n, j in self._jobs.items()]

    def start(self, names: list[str] | None = None) -> list[dict[str, str]]:
        # Require explicit names — never start "all peers" by default.
        if not names:
            return []
        selected = {p.name: p for p in ANALYSIS_STIMULI if p.name in names}
        started: list[dict[str, str]] = []
        for name, spec in selected.items():
            if name in self._jobs:
                started.append(
                    {"name": name, "job_id": self._jobs[name], "status": "already"}
                )
                continue
            job_id = self._scheduler.schedule(
                bus=spec.bus,
                key=spec.key,
                values=dict(spec.values),
                period_ms=spec.period_ms,
                owner=f"analysis:{name}",
                source=FrameSource.INJECTION,
                counter_field=spec.counter_field,
            )
            self._jobs[name] = job_id
            started.append({"name": name, "job_id": job_id, "status": "started"})
        return started

    def start_host_drive(
        self,
        *,
        speed_mmps: int = 0,
        yaw_rate_mrad_s: int = 0,
        gear: int = 1,
        period_ms: float = 100,
    ) -> dict[str, str]:
        """Schedule HOST_DRIVE_CMD for yaw/speed analysis (re-encode each period)."""
        name = "host_drive_analysis"
        if name in self._jobs:
            self._scheduler.cancel(self._jobs[name])
            del self._jobs[name]
        job_id = self._scheduler.schedule(
            bus="high",
            key="host:host_drive_cmd",
            values={
                "speed_mmps": speed_mmps,
                "yaw_rate_mrad_s": yaw_rate_mrad_s,
                "gear": gear,
            },
            period_ms=period_ms,
            owner="analysis:host_drive",
            source=FrameSource.INJECTION,
        )
        self._jobs[name] = job_id
        return {"name": name, "job_id": job_id, "status": "started"}

    def stop(self, names: list[str] | None = None) -> int:
        targets = list(self._jobs.keys()) if names is None else names
        n = 0
        for name in targets:
            job_id = self._jobs.pop(name, None)
            if job_id is not None:
                self._scheduler.cancel(job_id)
                n += 1
        return n

    def stop_all(self) -> int:
        return self.stop(None)
