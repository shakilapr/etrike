"""Internal event distribution for WebSocket clients.

Fan-out of latest-state snapshots and critical events to subscribers with
per-client bounded queues. Slow clients drop oldest coalesced state rather than
blocking the router (architecture streaming rules).
"""

from __future__ import annotations

import asyncio
import itertools
from typing import Any


class EventBus:
    """In-process pub/sub with independent per-subscriber queues."""

    def __init__(self, maxsize: int = 64) -> None:
        self._maxsize = maxsize
        self._subs: dict[int, asyncio.Queue[dict[str, Any]]] = {}
        self._ids = itertools.count(1)
        self._lock = asyncio.Lock()

    async def subscribe(self) -> tuple[int, asyncio.Queue[dict[str, Any]]]:
        q: asyncio.Queue[dict[str, Any]] = asyncio.Queue(maxsize=self._maxsize)
        async with self._lock:
            sid = next(self._ids)
            self._subs[sid] = q
        return sid, q

    async def unsubscribe(self, sid: int) -> None:
        async with self._lock:
            self._subs.pop(sid, None)

    async def publish(self, event: dict[str, Any]) -> None:
        async with self._lock:
            targets = list(self._subs.items())
        for _sid, q in targets:
            try:
                q.put_nowait(event)
            except asyncio.QueueFull:
                # Drop oldest then retry once — never block publishers.
                try:
                    q.get_nowait()
                except asyncio.QueueEmpty:
                    pass
                try:
                    q.put_nowait(event)
                except asyncio.QueueFull:
                    pass
