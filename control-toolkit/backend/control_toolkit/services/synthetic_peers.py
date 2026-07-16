"""Minimal synthetic peer matrix for Pure Software / bench missing peers."""

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


# Static safe defaults used to keep watchdogs fed — not full ECU simulation.
DEFAULT_PEERS: tuple[PeerSpec, ...] = (
    PeerSpec(
        "host_heartbeat",
        "high",
        "host:host_heartbeat",
        500,
        {"alive_ctr": 0, "health_flags": 0},
        "alive_ctr",
    ),
    PeerSpec(
        "sys_heartbeat",
        "low",
        "sys:sys_heartbeat",
        100,
        {
            "alive_ctr": 0,
            "heartbeat_ok": 1,
            "estop_active": 0,
            "mode_auto": 0,
            "can_ok": 1,
            "task_safety_ok": 1,
            "task_brake_ok": 1,
            "task_dispatch_ok": 1,
            "task_can_tx_ok": 1,
        },
        "alive_ctr",
    ),
    PeerSpec(
        "rt_heartbeat_high",
        "high",
        "rt:rt_heartbeat",
        500,
        {"alive_ctr": 0, "health_flags": 0},
        "alive_ctr",
    ),
    PeerSpec(
        "rt_heartbeat_low",
        "low",
        "rt:rt_heartbeat",
        500,
        {"alive_ctr": 0, "health_flags": 0},
        "alive_ctr",
    ),
)


class SyntheticPeerService:
    def __init__(self, scheduler: Scheduler) -> None:
        self._scheduler = scheduler
        self._jobs: dict[str, str] = {}  # peer name -> job id

    def list_running(self) -> list[dict[str, str]]:
        return [{"name": n, "job_id": j} for n, j in self._jobs.items()]

    def start(self, names: list[str] | None = None) -> list[dict[str, str]]:
        selected = {
            p.name: p
            for p in DEFAULT_PEERS
            if names is None or p.name in names
        }
        started: list[dict[str, str]] = []
        for name, spec in selected.items():
            if name in self._jobs:
                started.append({"name": name, "job_id": self._jobs[name], "status": "already"})
                continue
            job_id = self._scheduler.schedule(
                bus=spec.bus,
                key=spec.key,
                values=dict(spec.values),
                period_ms=spec.period_ms,
                owner=f"synthetic:{name}",
                source=FrameSource.SYNTHETIC,
                counter_field=spec.counter_field,
            )
            self._jobs[name] = job_id
            started.append({"name": name, "job_id": job_id, "status": "started"})
        return started

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
