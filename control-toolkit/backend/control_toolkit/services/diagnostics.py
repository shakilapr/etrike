"""Diagnostic event log and simple episode aggregation (Phase 6 software track)."""

from __future__ import annotations

import threading
import time
import uuid
from collections import deque
from dataclasses import dataclass, field
from typing import Any, Callable


@dataclass
class DiagnosticEvent:
    event_id: str
    code: str
    severity: str  # info | warning | error | critical
    title: str
    detail: str
    created_mono: float
    bus: str | None = None
    can_id: int | None = None
    correlation_id: str | None = None
    evidence: dict[str, Any] = field(default_factory=dict)


@dataclass
class Episode:
    episode_id: str
    code: str
    scope: str
    first_mono: float
    last_mono: float
    count: int = 1
    recovered: bool = False
    title: str = ""
    severity: str = "warning"


class DiagnosticsService:
    def __init__(
        self,
        capacity: int = 2000,
        *,
        recovery_hysteresis_s: float = 0.5,
        on_emit: Callable[[DiagnosticEvent], None] | None = None,
    ) -> None:
        self._lock = threading.Lock()
        self._events: deque[DiagnosticEvent] = deque(maxlen=capacity)
        self._episodes: dict[str, Episode] = {}
        # Pending recoveries: key -> first recovery request mono
        self._pending_recover: dict[str, float] = {}
        self._recovery_hysteresis_s = recovery_hysteresis_s
        self._on_emit = on_emit

    def emit(
        self,
        *,
        code: str,
        title: str,
        detail: str = "",
        severity: str = "info",
        bus: str | None = None,
        can_id: int | None = None,
        correlation_id: str | None = None,
        evidence: dict[str, Any] | None = None,
    ) -> DiagnosticEvent:
        now = time.monotonic()
        ev = DiagnosticEvent(
            event_id=f"evt_{uuid.uuid4().hex[:12]}",
            code=code,
            severity=severity,
            title=title,
            detail=detail,
            created_mono=now,
            bus=bus,
            can_id=can_id,
            correlation_id=correlation_id,
            evidence=evidence or {},
        )
        with self._lock:
            self._events.appendleft(ev)
            if severity in ("warning", "error", "critical"):
                # Re-fault cancels pending recovery hysteresis.
                scope = bus or "global"
                self._pending_recover.pop(f"{code}|{scope}", None)
                self._touch_episode_locked(ev, now)
        if self._on_emit is not None:
            try:
                self._on_emit(ev)
            except Exception:
                pass
        return ev

    def recover(
        self, code: str, scope: str = "global", *, force: bool = False
    ) -> bool:
        """Mark episode recovered after recovery hysteresis (anti-chatter).

        Returns True when recovery is committed. Pass ``force=True`` to skip
        hysteresis (tests / explicit clear).
        """
        key = f"{code}|{scope}"
        now = time.monotonic()
        with self._lock:
            ep = self._episodes.get(key)
            if ep is None or ep.recovered:
                self._pending_recover.pop(key, None)
                return bool(ep and ep.recovered)
            if force:
                ep.recovered = True
                ep.last_mono = now
                self._pending_recover.pop(key, None)
                return True
            pending = self._pending_recover.get(key)
            if pending is None:
                self._pending_recover[key] = now
                return False
            if now - pending < self._recovery_hysteresis_s:
                return False
            ep.recovered = True
            ep.last_mono = now
            self._pending_recover.pop(key, None)
            return True

    def list_events(
        self,
        *,
        limit: int = 100,
        code: str | None = None,
        severity: str | None = None,
    ) -> list[dict[str, Any]]:
        with self._lock:
            items = list(self._events)
        out: list[dict[str, Any]] = []
        for e in items:
            if code and e.code != code:
                continue
            if severity and e.severity != severity:
                continue
            out.append(self._event_dict(e))
            if len(out) >= limit:
                break
        return out

    def get_event(self, event_id: str) -> dict[str, Any] | None:
        with self._lock:
            for e in self._events:
                if e.event_id == event_id:
                    return self._event_dict(e)
        return None

    def list_episodes(self) -> list[dict[str, Any]]:
        with self._lock:
            eps = list(self._episodes.values())
        return [
            {
                "episode_id": e.episode_id,
                "code": e.code,
                "scope": e.scope,
                "count": e.count,
                "recovered": e.recovered,
                "title": e.title,
                "severity": e.severity,
                "age_s": time.monotonic() - e.last_mono,
            }
            for e in sorted(eps, key=lambda x: x.last_mono, reverse=True)
        ]

    def _touch_episode_locked(self, ev: DiagnosticEvent, now: float) -> None:
        scope = ev.bus or "global"
        key = f"{ev.code}|{scope}"
        ep = self._episodes.get(key)
        if ep is None or ep.recovered:
            self._episodes[key] = Episode(
                episode_id=f"ep_{uuid.uuid4().hex[:10]}",
                code=ev.code,
                scope=scope,
                first_mono=now,
                last_mono=now,
                title=ev.title,
                severity=ev.severity,
            )
        else:
            ep.count += 1
            ep.last_mono = now
            if ev.severity == "critical":
                ep.severity = "critical"

    @staticmethod
    def _event_dict(e: DiagnosticEvent) -> dict[str, Any]:
        return {
            "event_id": e.event_id,
            "code": e.code,
            "severity": e.severity,
            "title": e.title,
            "detail": e.detail,
            "bus": e.bus,
            "can_id": e.can_id,
            "correlation_id": e.correlation_id,
            "evidence": e.evidence,
            "age_s": time.monotonic() - e.created_mono,
        }
