"""Source ownership and stimulus leases (workplan §3.4).

One permitted producer per ``bus + CAN ID`` during a session. Leases expire on
their own — a crashed or disconnected client cannot wedge an ID forever — and
renewal is explicit so an interactive control only holds ownership while it is
actually current.
"""

from __future__ import annotations

import threading
import time
import uuid
from dataclasses import dataclass


@dataclass
class Lease:
    lease_id: str
    owner: str
    bus: str
    can_id: int
    resource: str
    expires_at_mono: float
    renew_s: float = 2.0


class OwnershipConflict(Exception):
    pass


class OwnershipTable:
    """Exclusive ownership of ``(bus, can_id)`` with expiring leases."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._by_key: dict[tuple[str, int], Lease] = {}
        self._by_id: dict[str, Lease] = {}

    def claim(
        self,
        *,
        bus: str,
        can_id: int,
        owner: str,
        resource: str | None = None,
        ttl_s: float = 5.0,
    ) -> Lease:
        key = (bus, int(can_id))
        now = time.monotonic()
        with self._lock:
            self._expire_locked(now)
            existing = self._by_key.get(key)
            if existing is not None and existing.owner != owner:
                raise OwnershipConflict(f"{bus} 0x{can_id:X} owned by {existing.owner}")
            if existing is not None:
                existing.expires_at_mono = now + ttl_s
                return existing
            lease = Lease(
                lease_id=f"lease_{uuid.uuid4().hex[:10]}",
                owner=owner,
                bus=bus,
                can_id=int(can_id),
                resource=resource or f"{bus}:{can_id:X}",
                expires_at_mono=now + ttl_s,
                renew_s=ttl_s,
            )
            self._by_key[key] = lease
            self._by_id[lease.lease_id] = lease
            return lease

    def renew(self, lease_id: str, ttl_s: float | None = None) -> Lease:
        now = time.monotonic()
        with self._lock:
            lease = self._by_id.get(lease_id)
            if lease is None:
                raise KeyError(lease_id)
            lease.expires_at_mono = now + (ttl_s if ttl_s is not None else lease.renew_s)
            return lease

    def release(self, lease_id: str) -> None:
        with self._lock:
            lease = self._by_id.pop(lease_id, None)
            if lease is None:
                return
            key = (lease.bus, lease.can_id)
            if self._by_key.get(key) is lease:
                del self._by_key[key]

    def release_owner(self, owner: str) -> int:
        with self._lock:
            ids = [lid for lid, lease in self._by_id.items() if lease.owner == owner]
            for lid in ids:
                lease = self._by_id.pop(lid)
                key = (lease.bus, lease.can_id)
                if self._by_key.get(key) is lease:
                    del self._by_key[key]
            return len(ids)

    def clear(self) -> None:
        with self._lock:
            self._by_key.clear()
            self._by_id.clear()

    def list_ids(self) -> list[str]:
        with self._lock:
            self._expire_locked(time.monotonic())
            return list(self._by_id.keys())

    def owner_of(self, bus: str, can_id: int) -> str | None:
        with self._lock:
            self._expire_locked(time.monotonic())
            lease = self._by_key.get((bus, int(can_id)))
            return lease.owner if lease else None

    def _expire_locked(self, now: float) -> None:
        expired = [lid for lid, lease in self._by_id.items() if lease.expires_at_mono <= now]
        for lid in expired:
            lease = self._by_id.pop(lid)
            key = (lease.bus, lease.can_id)
            if self._by_key.get(key) is lease:
                del self._by_key[key]
