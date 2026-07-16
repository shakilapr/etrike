"""Operational audit log (architecture §7, §14.1).

Separate from:
  - Live CAN / history (high-rate frames)
  - Recording (raw RX/TX evidence buffer)
  - Diagnostics episodes (aggregated fault conditions)

This ring buffer keeps a human-readable, filterable trail of *everything that
happened in the toolkit*: session, transport, control, inject, safety,
recording, tests, protocol. UI Logging tab and headless clients query it.
"""

from __future__ import annotations

import threading
import time
import uuid
from collections import deque
from dataclasses import dataclass, field
from typing import Any


# Categories align with architecture always-on event subscription themes.
CATEGORIES = frozenset(
    {
        "system",
        "session",
        "transport",
        "control",
        "inject",
        "safety",
        "recording",
        "test",
        "protocol",
        "hmi",
        "api",
    }
)


@dataclass
class AuditEntry:
    log_id: str
    ts_mono: float
    ts_wall: float
    category: str
    severity: str  # debug | info | warning | error | critical
    code: str
    title: str
    detail: str = ""
    bus: str | None = None
    can_id: int | None = None
    session_id: str | None = None
    correlation_id: str | None = None
    source: str = "backend"
    data: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return {
            "log_id": self.log_id,
            "ts_mono": self.ts_mono,
            "ts_wall": self.ts_wall,
            "age_s": max(0.0, time.monotonic() - self.ts_mono),
            "category": self.category,
            "severity": self.severity,
            "code": self.code,
            "title": self.title,
            "detail": self.detail,
            "bus": self.bus,
            "can_id": self.can_id,
            "session_id": self.session_id,
            "correlation_id": self.correlation_id,
            "source": self.source,
            "data": dict(self.data),
        }


class AuditLogService:
    def __init__(self, capacity: int = 10_000) -> None:
        self._lock = threading.Lock()
        self._entries: deque[AuditEntry] = deque(maxlen=capacity)
        self._capacity = capacity
        self._seq = 0

    def log(
        self,
        *,
        category: str,
        code: str,
        title: str,
        detail: str = "",
        severity: str = "info",
        bus: str | None = None,
        can_id: int | None = None,
        session_id: str | None = None,
        correlation_id: str | None = None,
        source: str = "backend",
        data: dict[str, Any] | None = None,
    ) -> AuditEntry:
        cat = category if category in CATEGORIES else "system"
        entry = AuditEntry(
            log_id=f"log_{uuid.uuid4().hex[:12]}",
            ts_mono=time.monotonic(),
            ts_wall=time.time(),
            category=cat,
            severity=severity,
            code=code,
            title=title,
            detail=detail,
            bus=bus,
            can_id=can_id,
            session_id=session_id,
            correlation_id=correlation_id,
            source=source,
            data=data or {},
        )
        with self._lock:
            self._seq += 1
            self._entries.appendleft(entry)
        return entry

    def list_logs(
        self,
        *,
        limit: int = 200,
        category: str | None = None,
        severity: str | None = None,
        code: str | None = None,
        q: str | None = None,
        since_mono: float | None = None,
    ) -> list[dict[str, Any]]:
        with self._lock:
            items = list(self._entries)
        out: list[dict[str, Any]] = []
        needle = (q or "").strip().lower()
        for e in items:
            if category and e.category != category:
                continue
            if severity and e.severity != severity:
                continue
            if code and e.code != code:
                continue
            if since_mono is not None and e.ts_mono < since_mono:
                continue
            if needle:
                blob = f"{e.code} {e.title} {e.detail} {e.category}".lower()
                if needle not in blob:
                    continue
            out.append(e.to_dict())
            if len(out) >= limit:
                break
        return out

    def clear(self) -> int:
        with self._lock:
            n = len(self._entries)
            self._entries.clear()
            return n

    def stats(self) -> dict[str, Any]:
        with self._lock:
            by_cat: dict[str, int] = {}
            by_sev: dict[str, int] = {}
            for e in self._entries:
                by_cat[e.category] = by_cat.get(e.category, 0) + 1
                by_sev[e.severity] = by_sev.get(e.severity, 0) + 1
            return {
                "count": len(self._entries),
                "capacity": self._capacity,
                "sequence": self._seq,
                "by_category": by_cat,
                "by_severity": by_sev,
            }
