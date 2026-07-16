"""Startup/shutdown orchestration for Pure Software backend services."""

from __future__ import annotations

import asyncio
import contextlib
import time

from control_toolkit.config import Profile, ToolkitConfig
from control_toolkit.pipeline.freshness import FreshnessAger
from control_toolkit.pipeline.router import Router
from control_toolkit import protocol_bridge as proto
from control_toolkit.services.control_intent import ControlIntentService
from control_toolkit.services.diagnostics import DiagnosticsService
from control_toolkit.services.event_bus import EventBus
from control_toolkit.services.ownership import OwnershipTable
from control_toolkit.services.recording import RecordingService
from control_toolkit.services.scheduler import Scheduler
from control_toolkit.services.session_manager import SessionManager
from control_toolkit.services.synthetic_peers import SyntheticPeerService
from control_toolkit.services.tx_gate import TxGate
from control_toolkit.state.history import FrameHistory
from control_toolkit.state.latest import LatestStore
from control_toolkit.state.topology import TopologyTracker
from control_toolkit.transport.virtual import VirtualTransportAdapter


class Lifecycle:
    def __init__(self, config: ToolkitConfig) -> None:
        self.config = config
        self.latest = LatestStore()
        self.history = FrameHistory(capacity=getattr(config, "history_capacity", 4096))
        self.topology = TopologyTracker()
        self.events = EventBus()
        self.ownership = OwnershipTable()
        self.recording = RecordingService()
        self.diagnostics = DiagnosticsService()
        self.transport: VirtualTransportAdapter | None = None
        self.router: Router | None = None
        self.ager: FreshnessAger | None = None
        self.tx_gate = TxGate(
            ownership=self.ownership,
            get_profile=lambda: self.sessions.active_profile()
            if getattr(self, "sessions", None)
            else config.default_profile,
            get_bench_tx=lambda: self.sessions.bench_tx(),
            get_transport=lambda: self.transport,
            get_adapter_epoch=lambda: self.transport.epoch if self.transport else None,
        )
        self.scheduler = Scheduler(self.tx_gate)
        self.synthetic = SyntheticPeerService(self.scheduler)
        self.control = ControlIntentService(
            tx_gate=self.tx_gate,
            scheduler=self.scheduler,
            require_bench_tx=lambda: self.sessions.require_bench_tx_enabled(),
        )
        self.sessions = SessionManager(
            ownership=self.ownership,
            get_transport_open=lambda: self.transport is not None,
            get_adapter_epoch=lambda: self.transport.epoch if self.transport else None,
            on_stop_all=self._on_stop_all,
        )
        self.sessions.bind_scheduler(self.scheduler)
        self._tasks: list[asyncio.Task] = []
        self._ready = False

    def _on_stop_all(self) -> None:
        self.synthetic.stop_all()
        self.control.release(reason="stop_all")
        self.diagnostics.emit(
            code="session.stop_all",
            title="Stop All",
            detail="Bench TX disabled; jobs and leases cleared",
            severity="warning",
        )

    def _record_frame(self, env) -> None:
        self.recording.observe_frame(
            bus=env.channel.value,
            can_id=env.can_id,
            dlc=env.dlc,
            data=env.data,
            direction=env.direction.value,
            source=env.source.value,
            backend_arrival_ns=env.backend_arrival_ns,
            adapter_epoch=env.adapter_epoch,
        )

    async def startup(self) -> None:
        self.ager = FreshnessAger(self.latest)
        self._tasks.append(asyncio.create_task(self.ager.run()))
        self._tasks.append(asyncio.create_task(self._topology_loop()))

        if self.config.default_profile is Profile.PURE_SOFTWARE:
            self.transport = VirtualTransportAdapter(
                rx_queue_maxsize=self.config.rx_queue_maxsize
            )
            self.transport.open()
            self.router = Router(
                self.transport,
                self.latest,
                history=self.history,
                topology=self.topology,
                on_frame=self._record_frame,
            )
            self._tasks.append(asyncio.create_task(self.router.run()))

        self.scheduler.start()
        self.diagnostics.emit(
            code="backend.ready",
            title="Backend ready",
            detail=f"profile={self.config.default_profile.value} wire={proto.WIRE_HASH[:12]}",
            severity="info",
        )
        self._tasks.append(asyncio.create_task(self._control_watchdog_loop()))
        self._tasks.append(asyncio.create_task(self._broadcast_loop()))
        self._ready = True

    async def _control_watchdog_loop(self) -> None:
        try:
            while True:
                await asyncio.sleep(0.05)
                if self._ready:
                    self.control.tick_watchdog()
        except asyncio.CancelledError:
            return

    async def _topology_loop(self) -> None:
        try:
            while True:
                await asyncio.sleep(0.1)
                if not self._ready:
                    continue
                self.topology.reclassify(
                    self.latest.get_messages_map(), time.monotonic_ns()
                )
        except asyncio.CancelledError:
            return

    async def _broadcast_loop(self) -> None:
        interval = 1.0 / max(1, self.config.latest_state_batch_hz)
        heartbeat_s = self.config.stream_heartbeat_ms / 1000.0
        last_heartbeat = 0.0
        try:
            while True:
                await asyncio.sleep(interval)
                if not self._ready:
                    continue
                snap = self.latest.snapshot()
                await self.events.publish(
                    {
                        "type": "state",
                        "sequence": snap.sequence,
                        "wire_hash": snap.wire_hash,
                        "messages": [m.model_dump(mode="json") for m in snap.messages],
                        "session": self.sessions.snapshot().model_dump(mode="json"),
                    }
                )
                now = time.monotonic()
                if now - last_heartbeat >= heartbeat_s:
                    last_heartbeat = now
                    await self.events.publish(
                        {
                            "type": "heartbeat",
                            "monotonic_ns": time.monotonic_ns(),
                            "wire_hash": snap.wire_hash,
                        }
                    )
        except asyncio.CancelledError:
            return

    async def shutdown(self) -> None:
        self._ready = False
        try:
            self.sessions.close()
        except Exception:
            pass
        self.scheduler.stop()
        self.synthetic.stop_all()
        self.ownership.clear()
        if self.router is not None:
            self.router.stop()
        if self.ager is not None:
            self.ager.stop()
        for task in self._tasks:
            task.cancel()
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
