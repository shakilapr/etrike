"""Startup/shutdown orchestration (workplan §1.1).

Owns the singletons shared across requests: latest-value store, transport, and
router (Phase 1). Attached to ``app.state`` by the app factory and driven by the
FastAPI lifespan.

In the Pure Software profile, startup opens a virtual dual-bus transport and
starts the router drain loop, so injected frames flow to the latest-value store
with no hardware. Physical profiles (Bench Test / Full Vehicle) open the
CANalyst-II transport in Phase 2+.
"""

from __future__ import annotations

import asyncio
import contextlib

from vtc.config import Profile, VtcConfig
from vtc.pipeline.freshness import FreshnessAger
from vtc.pipeline.router import Router
from vtc.services.event_bus import EventBus
from vtc.state.latest import LatestStore
from vtc.transport.virtual import VirtualTransportAdapter


class Lifecycle:
    def __init__(self, config: VtcConfig) -> None:
        self.config = config
        self.latest = LatestStore()
        self.event_bus = EventBus()
        self.transport: VirtualTransportAdapter | None = None
        self.router: Router | None = None
        self.ager: FreshnessAger | None = None
        self._tasks: list[asyncio.Task] = []
        self._ready = False

    async def startup(self) -> None:
        # Freshness aging runs in every profile so state ages even before/without
        # a transport; the router+transport only run in Pure Software (Phase 1).
        self.ager = FreshnessAger(self.latest)
        self._tasks.append(asyncio.create_task(self.ager.run()))

        if self.config.default_profile is Profile.PURE_SOFTWARE:
            self.transport = VirtualTransportAdapter(
                rx_queue_maxsize=self.config.rx_queue_maxsize
            )
            self.transport.open()
            self.router = Router(self.transport, self.latest)
            self._tasks.append(asyncio.create_task(self.router.run()))
        self._ready = True

    async def shutdown(self) -> None:
        self._ready = False
        if self.router is not None:
            self.router.stop()
        if self.ager is not None:
            self.ager.stop()
        for task in self._tasks:
            with contextlib.suppress(asyncio.TimeoutError, asyncio.CancelledError):
                await asyncio.wait_for(task, timeout=2.0)
        self._tasks.clear()
        if self.transport is not None:
            self.transport.close()
            self.transport = None
        self.router = None
        self.ager = None

    @property
    def ready(self) -> bool:
        return self._ready
