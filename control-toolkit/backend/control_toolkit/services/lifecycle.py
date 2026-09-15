"""Startup/shutdown orchestration for Pure Software backend services."""

from __future__ import annotations

import asyncio
import contextlib
import time

from control_toolkit.config import Profile, ToolkitConfig
from control_toolkit.models.adapter import AdapterHealth
from control_toolkit.pipeline.freshness import FreshnessAger
from control_toolkit.pipeline.router import Router
from control_toolkit import protocol_bridge as proto
from control_toolkit.services.control_intent import ControlIntentService
from control_toolkit.services.audit_log import AuditLogService
from control_toolkit.services.diagnostics import DiagnosticsService
from control_toolkit.services.estop_monitor import EstopEventMonitor
from control_toolkit.services.event_bus import EventBus
from control_toolkit.services.ownership import OwnershipTable
from control_toolkit.services.recording import RecordingService
from control_toolkit.services.scheduler import Scheduler
from control_toolkit.services.session_manager import SessionManager
from control_toolkit.services.synthetic_peers import SyntheticPeerService
from control_toolkit.services.tx_gate import TxGate
from control_toolkit.services.verification import VerificationService
from control_toolkit.state.history import FrameHistory
from control_toolkit.state.latest import LatestStore
from control_toolkit.state.storage import SqliteStorage
from control_toolkit.state.topology import TopologyTracker
from control_toolkit.transport.canalyst import (
    CanalystDiscovery,
    CanalystTransportAdapter,
    discover_canalyst,
)


class Lifecycle:
    def __init__(self, config: ToolkitConfig, bus_factory: Any | None = None) -> None:
        self.config = config
        self._bus_factory = bus_factory
        self.latest = LatestStore()
        self.history = FrameHistory(capacity=getattr(config, "history_capacity", 4096))
        self.topology = TopologyTracker(on_liveness_change=self._on_topology_liveness_change)
        self.storage = SqliteStorage("history.sqlite")
        self.events = EventBus()
        self.ownership = OwnershipTable()
        self.recording = RecordingService()
        self.audit = AuditLogService()
        self.diagnostics = DiagnosticsService(on_emit=self._mirror_diag_to_audit)
        self.estop_monitor = EstopEventMonitor(
            self.diagnostics,
            get_session_id=lambda: self.sessions.snapshot().session_id
            if getattr(self, "sessions", None)
            else None,
        )
        self.transport: CanalystTransportAdapter | None = None
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
            get_transport_open=self._transport_open,
            get_adapter_epoch=lambda: self.transport.epoch if self.transport else None,
            on_stop_all=self._on_stop_all,
            on_profile_change=self._on_profile_change,
            physical_available=self.physical_available,
        )
        self.sessions.bind_scheduler(self.scheduler)
        self.verification = VerificationService(
            tx_gate=self.tx_gate,
            latest=self.latest,
            recording=self.recording,
            require_bench_tx=lambda: self.sessions.require_bench_tx_enabled(),
        )
        self._tasks: list[asyncio.Task] = []
        self._ready = False
        self._router_task: asyncio.Task | None = None
        self._loop: asyncio.AbstractEventLoop | None = None

    def _transport_open(self) -> bool:
        if self.transport is None:
            return False
        try:
            return self.transport.status().health in (
                AdapterHealth.OPEN,
                AdapterHealth.ACTIVE,
                AdapterHealth.QUIET,
            )
        except Exception:
            return False

    def physical_available(self) -> tuple[bool, str]:
        result = self.physical_discovery()
        return result.available, result.reason

    def physical_discovery(self, *, force: bool = False) -> CanalystDiscovery:
        # Never probe/open a second USB handle while the active physical
        # transport already owns the device.
        if isinstance(self.transport, CanalystTransportAdapter):
            status = self.transport.status()
            ok = status.health in (
                AdapterHealth.OPEN,
                AdapterHealth.ACTIVE,
                AdapterHealth.QUIET,
            )
            reason = (
                "CANalyst-II is the active transport"
                if ok
                else status.last_error or f"CANalyst-II is {status.health.value}"
            )
            return CanalystDiscovery(
                available=ok,
                reason=reason,
                device_index=self.config.canalyst_device_index,
                bitrate=self.config.canalyst_bitrate,
                usb_visible=True if ok else None,
            )
        return discover_canalyst(
            device_index=self.config.canalyst_device_index,
            bitrate=self.config.canalyst_bitrate,
            force=force,
            bus_factory=self._bus_factory,
        )

    def is_physical_transport(self) -> bool:
        return isinstance(self.transport, CanalystTransportAdapter)

    def _mirror_diag_to_audit(self, ev) -> None:
        """Every diagnostic event also appears in the operational audit log."""
        cat = "system"
        code = getattr(ev, "code", "") or ""
        if code.startswith("session.") or code.startswith("recording."):
            cat = "session" if code.startswith("session.") else "recording"
        elif code.startswith("control.") or code.startswith("test."):
            cat = "control" if code.startswith("control.") else "test"
        elif code.startswith("transport.") or code.startswith("backend."):
            cat = "transport" if code.startswith("transport.") else "system"
        elif "estop" in code.lower() or code.startswith("safety."):
            cat = "safety"
        self.audit.log(
            category=cat,
            code=code,
            title=getattr(ev, "title", "") or code,
            detail=getattr(ev, "detail", "") or "",
            severity=getattr(ev, "severity", "info") or "info",
            bus=getattr(ev, "bus", None),
            can_id=getattr(ev, "can_id", None),
            correlation_id=getattr(ev, "correlation_id", None),
            data=dict(getattr(ev, "evidence", None) or {}),
            only_on_change=True,
        )

    def _on_topology_liveness_change(self, node, prev, curr) -> None:
        from control_toolkit.state.topology import NodeLiveness

        if prev is NodeLiveness.OFFLINE and curr is NodeLiveness.OFFLINE:
            return
        severity = "info"
        if curr in (NodeLiveness.MISSING, NodeLiveness.LATE):
            severity = "warning"
        elif curr is NodeLiveness.FAULT:
            severity = "error"

        self.diagnostics.emit(
            code="protocol.node_liveness",
            title=f"Node {node.node} ({node.bus.title()}) → {curr.value}",
            detail=f"ECU presence transition from {prev.value} to {curr.value}",
            severity=severity,
            bus=node.bus,
            can_id=node.can_id,
            evidence={
                "node": node.node,
                "previous_liveness": prev.value,
                "current_liveness": curr.value,
                "validation_status": node.validation_status,
            },
        )

    def _on_stop_all(self) -> None:
        self.synthetic.stop_all()
        self.control.release(reason="stop_all")
        self.control.clear_estop_flag()
        # Best-effort zero host command before TX is left disabled.
        # Skip during shutdown / when not ready to avoid nested lifecycle work.
        try:
            from control_toolkit.models.frames import FrameSource

            if self._ready and self._transport_open():
                self.tx_gate.submit(
                    bus="high",
                    key="host:host_drive_cmd",
                    values={
                        "speed_mmps": 0,
                        "yaw_rate_mrad_s": 0,
                        "gear": 0,
                    },
                    owner="session:stop_all",
                    source=FrameSource.INJECTION,
                    claim_ownership=False,
                    lease_ttl_s=0.5,
                )
        except Exception:  # noqa: BLE001
            pass
        # Host ESTOP latch is software-only; clear so Real/offline UI is not "ESTOP live".
        try:
            self.sessions.clear_estop_latch()
        except Exception:  # noqa: BLE001
            pass
        self.diagnostics.emit(
            code="session.stop_all",
            title="Stop All",
            detail="Bench TX disabled; jobs, leases, and host ESTOP latch cleared",
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
            is_extended=env.is_extended,
            is_remote=env.is_remote,
        )
        if self.storage:
            self.storage.append(env)

    def _start_router(self) -> None:
        if self.transport is None:
            return
        if self.router is not None:
            self.router.stop()
        self.router = Router(
            self.transport,
            self.latest,
            history=self.history,
            topology=self.topology,
            on_frame=self._record_frame,
            on_message=self.estop_monitor.observe,
        )
        if self._router_task is not None:
            self._router_task.cancel()
        # Sync FastAPI handlers run off the loop — schedule on the lifespan loop.
        loop = self._loop
        if loop is not None and loop.is_running():
            self._router_task = loop.create_task(self.router.run())
        else:
            try:
                self._router_task = asyncio.get_running_loop().create_task(
                    self.router.run()
                )
            except RuntimeError:
                # No loop yet (startup race); will attach on next open after loop set.
                self._router_task = None

    def open_physical_transport(self, *, require_device: bool = True) -> bool:
        """Open CANalyst-II High+Low.

        When ``require_device`` is True (startup force / explicit connect), missing
        hardware raises. When False (operator selected Real mode), a missing
        adapter tears down virtual traffic and leaves transport absent so the UI
        can show Real + no connection — never a silent virtual fallback.
        """
        if isinstance(self.transport, CanalystTransportAdapter):
            return True
        # Fast path for Real-without-adapter: USB VID/PID only (no python-can open).
        # Full driver open can hang for minutes when the device is absent.
        if not require_device and self._bus_factory is None:
            usb_visible = False
            try:
                import usb.core

                usb_visible = (
                    usb.core.find(idVendor=0x04D8, idProduct=0x0053, find_all=False)
                    is not None
                )
            except Exception:  # noqa: BLE001
                usb_visible = False
            if not usb_visible:
                self._tear_down_transport()
                self.transport = None
                detail = (
                    "CANalyst-II USB device 04D8:0053 not found; "
                    "Real mode active without physical link"
                )
                self.sessions.mark_link_absent(detail)
                self.diagnostics.emit(
                    code="transport.canalyst_absent",
                    title="CANalyst-II not connected",
                    detail=detail,
                    severity="warning",
                )
                return False
        candidate = CanalystTransportAdapter(
            rx_queue_maxsize=self.config.rx_queue_maxsize,
            bitrate=self.config.canalyst_bitrate,
            device_index=self.config.canalyst_device_index,
            poll_ms=self.config.canalyst_poll_ms,
            receive_timeout_ms=self.config.canalyst_receive_timeout_ms,
            reconnect_initial_ms=self.config.canalyst_reconnect_initial_ms,
            reconnect_max_ms=self.config.canalyst_reconnect_max_ms,
            recovery_stability_ms=self.config.canalyst_recovery_stability_ms,
            bus_factory=self._bus_factory,
            on_failure=self._physical_failure_from_worker,
            on_recovered=self._physical_recovered_from_worker,
        )
        try:
            # Bound open so a hung driver cannot stall the event loop forever.
            import concurrent.futures

            pool = concurrent.futures.ThreadPoolExecutor(max_workers=1)
            try:
                fut = pool.submit(candidate.open)
                fut.result(timeout=3.0 if not require_device else 8.0)
            except concurrent.futures.TimeoutError as exc:
                raise RuntimeError(
                    "CANalyst-II open timed out (device missing or driver hung)"
                ) from exc
            finally:
                pool.shutdown(wait=False, cancel_futures=True)
        except Exception as exc:
            if require_device:
                raise
            # Leave Real profile with no transport — not virtual CAN.
            try:
                candidate.close()
            except Exception:  # noqa: BLE001
                pass
            self._tear_down_transport()
            self.transport = None
            detail = str(exc)
            self.sessions.mark_link_absent(detail)
            self.diagnostics.emit(
                code="transport.canalyst_absent",
                title="CANalyst-II not connected",
                detail=f"Real mode active without adapter: {detail}",
                severity="warning",
            )
            return False
        self._tear_down_transport()
        self.transport = candidate
        self._start_router()
        self.sessions.mark_link_connected(candidate.epoch)
        self.diagnostics.emit(
            code="transport.canalyst_open",
            title="CANalyst-II open",
            detail="CH0=High CH1=Low @ 500 kbit/s",
            severity="info",
        )
        return True

    def _physical_failure_from_worker(self, reason: str) -> None:
        loop = self._loop
        if loop is not None and loop.is_running():
            loop.call_soon_threadsafe(self._handle_physical_failure, reason)
        else:
            self._handle_physical_failure(reason)

    def _handle_physical_failure(self, reason: str) -> None:
        self.sessions.transport_failed(reason)
        self.recording.mark_degraded(f"physical transport failure: {reason}")
        # Drop last-known frames so Live CAN does not look active without a bus.
        self._clear_live_observations(reason=f"physical failure: {reason}")
        self.diagnostics.emit(
            code="transport.canalyst_failed",
            title="CANalyst-II connection lost",
            detail=f"{reason}; Bench TX disabled and jobs/leases cleared",
            severity="error",
        )

    def _physical_recovered_from_worker(self, epoch: int) -> None:
        loop = self._loop
        if loop is not None and loop.is_running():
            loop.call_soon_threadsafe(self._handle_physical_recovered, epoch)
        else:
            self._handle_physical_recovered(epoch)

    def _handle_physical_recovered(self, epoch: int) -> None:
        self.sessions.transport_recovered(epoch)
        self.diagnostics.emit(
            code="transport.canalyst_recovered",
            title="CANalyst-II recovered receive-only",
            detail=f"adapter epoch={epoch}; Bench TX remains disabled",
            severity="warning",
        )

    def _clear_live_observations(self, *, reason: str) -> None:
        """Drop Computer/virtual ghosts so Real-without-adapter is not 'live CAN'."""
        self.latest.clear()
        self.history.clear()
        self.topology.clear()
        self.diagnostics.emit(
            code="state.observations_cleared",
            title="Live observations cleared",
            detail=reason,
            severity="info",
        )

    def _tear_down_transport(self) -> None:
        if self.router is not None:
            self.router.stop()
            self.router = None
        if self.transport is not None:
            try:
                self.transport.close()
            except Exception:
                pass
            self.transport = None
        # No transport ⇒ no live traffic; do not leave prior bus rows as "live".
        self.estop_monitor.reset()
        self._clear_live_observations(reason="transport torn down")

    def _on_profile_change(self, profile: Profile) -> None:
        """Switch transport for profile.

        Real profiles may open with no adapter (link absent).
        """
        try:
            # Neutralize software peers/jobs before tearing transport (no ghost TX).
            try:
                self.synthetic.stop_all()
                self.control.release(reason=f"profile→{profile.value}")
                self.control.clear_estop_flag()
                self.sessions.clear_estop_latch()
            except Exception:  # noqa: BLE001
                pass
            # Clear before switch so UI does not show previous mode's frames.
            self._clear_live_observations(reason=f"profile → {profile.value}")
            if profile in (Profile.FULL_VEHICLE, Profile.BENCH_TEST):
                # Operator can enter Real without hardware; TX stays blocked.
                self.open_physical_transport(require_device=False)
        except Exception as exc:  # noqa: BLE001
            self.diagnostics.emit(
                code="transport.profile_switch_failed",
                title="Transport switch failed",
                detail=str(exc),
                severity="error",
            )
            raise

    async def startup(self) -> None:
        self._loop = asyncio.get_running_loop()
        self.ager = FreshnessAger(self.latest)
        self._tasks.append(asyncio.create_task(self.ager.run()))
        self._tasks.append(asyncio.create_task(self._topology_loop()))
        await self.storage.start()

        # Open physical transport (require_device=False allows starting without hardware attached)
        self.open_physical_transport(require_device=False)

        # If router deferred, start now that loop is known.
        if self.transport is not None and self.router is not None and self._router_task is None:
            self._router_task = asyncio.create_task(self.router.run())

        self.scheduler.start()
        adapter = self.transport.status().identity if self.transport else "none"
        self.diagnostics.emit(
            code="backend.ready",
            title="Backend ready",
            detail=(
                f"profile={self.config.default_profile.value} "
                f"adapter={adapter} wire={proto.WIRE_HASH[:12]}"
            ),
            severity="info",
        )
        self._tasks.append(asyncio.create_task(self._control_watchdog_loop()))
        self._tasks.append(asyncio.create_task(self._physical_link_probe_loop()))
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

    async def _physical_link_probe_loop(self) -> None:
        """While in Real with no open adapter, retry CANalyst open periodically.

        USB open is blocking — run in a worker thread so the event loop can
        still cancel tasks during shutdown.
        """
        try:
            while True:
                await asyncio.sleep(2.0)
                if not self._ready:
                    continue
                profile = self.sessions.active_profile()
                if profile not in (Profile.FULL_VEHICLE, Profile.BENCH_TEST):
                    continue
                if isinstance(self.transport, CanalystTransportAdapter):
                    continue
                try:
                    opened = await asyncio.to_thread(
                        self.open_physical_transport, require_device=False
                    )
                    if opened:
                        self.diagnostics.emit(
                            code="transport.canalyst_auto_connected",
                            title="CANalyst-II connected",
                            detail="Physical link established while in Real mode",
                            severity="info",
                        )
                except Exception as exc:  # noqa: BLE001
                    self.diagnostics.emit(
                        code="transport.canalyst_probe_error",
                        title="CANalyst probe error",
                        detail=str(exc),
                        severity="warning",
                    )
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
                session = self.sessions.snapshot()
                adapter = (
                    self.transport.status()
                    if self.transport is not None
                    else None
                )
                health = (
                    adapter.health.value
                    if adapter is not None
                    else "absent"
                )
                dest = session.destination or "physical"
                link = {
                    "mode": "real",
                    "destination": dest,
                    "connected": health
                    in ("open", "active", "quiet", "degraded", "recovering"),
                    "health": health,
                    "detail": (
                        None
                        if adapter is None
                        else adapter.last_error
                    )
                    if health != "absent"
                    else "CANalyst-II not connected — Real mode, no physical link",
                }
                await self.events.publish(
                    {
                        "type": "state",
                        "sequence": snap.sequence,
                        "wire_hash": snap.wire_hash,
                        "messages": [m.model_dump(mode="json") for m in snap.messages],
                        "session": session.model_dump(mode="json"),
                        "link": link,
                        "adapter": (
                            adapter.model_dump(mode="json")
                            if adapter is not None
                            else {
                                "identity": "none",
                                "health": "absent",
                                "last_error": link.get("detail"),
                                "channels": {},
                            }
                        ),
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
                            "link": link,
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
        if self.storage:
            await self.storage.stop()
        if self.router is not None:
            self.router.stop()
        if self._router_task is not None:
            self._router_task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self._router_task
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
