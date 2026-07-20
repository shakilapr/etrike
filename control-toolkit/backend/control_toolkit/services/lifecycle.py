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
from control_toolkit.services.event_bus import EventBus
from control_toolkit.services.ownership import OwnershipTable
from control_toolkit.services.recording import RecordingService
from control_toolkit.services.scheduler import Scheduler
from control_toolkit.services.session_manager import SessionManager
from control_toolkit.services.synthetic_peers import SyntheticPeerService
from control_toolkit.services.tx_gate import TxGate
from control_toolkit.services.verification import VerificationService
from control_toolkit.services.native_sil import NativeSilBridge
from control_toolkit.state.history import FrameHistory
from control_toolkit.state.latest import LatestStore
from control_toolkit.state.topology import TopologyTracker
from control_toolkit.transport.canalyst import (
    CanalystDiscovery,
    CanalystTransportAdapter,
    discover_canalyst,
)
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
        self.audit = AuditLogService()
        self.diagnostics = DiagnosticsService(on_emit=self._mirror_diag_to_audit)
        self.transport: VirtualTransportAdapter | CanalystTransportAdapter | None = None
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
        self.native_sil: NativeSilBridge | None = None

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
        )

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
            is_extended=env.is_extended,
            is_remote=env.is_remote,
        )

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

    def open_virtual_transport(self) -> None:
        """Open Pure Software dual virtual buses (no hardware)."""
        if isinstance(self.transport, VirtualTransportAdapter):
            return
        candidate = VirtualTransportAdapter(
            rx_queue_maxsize=self.config.rx_queue_maxsize
        )
        candidate.open()
        self._tear_down_transport()
        self.transport = candidate
        self._start_router()
        if self.config.native_sil_executable:
            self.start_native_sil()
        self.diagnostics.emit(
            code="transport.virtual_open",
            title="Virtual buses open",
            detail="High+Low virtual CAN",
            severity="info",
        )
    def _native_sil_error(self, detail: str) -> None:
        self.diagnostics.emit(
            code="simulation.native_sil_error",
            title="Native RT SIL error",
            detail=detail,
            severity="error",
        )

    def simulation_status(self) -> dict:
        """Truthful software-runtime status; never infer ECU health from bus traffic."""
        profile = self.sessions.active_profile()
        virtual = isinstance(self.transport, VirtualTransportAdapter)
        bridge = self.native_sil
        executable = (
            str(bridge.executable)
            if bridge is not None
            else self.config.native_sil_executable
        )
        return {
            "mode": "computer" if profile is Profile.PURE_SOFTWARE else "real",
            "profile": profile.value,
            "backend": {"state": "running" if self.ready else "starting"},
            "virtual_can": {
                "state": "running" if virtual and self._transport_open() else "stopped",
                "available": profile is Profile.PURE_SOFTWARE,
            },
            "router": {
                "state": "running"
                if self.router is not None and self._router_task is not None
                else "stopped"
            },
            "rt_sil": {
                "state": (
                    "running"
                    if bridge is not None and bridge.running
                    else "error"
                    if bridge is not None and bridge.last_error
                    else "stopped"
                ),
                "configured": bool(self.config.native_sil_executable),
                "available": bool(executable),
                "executable": executable,
                "pid": bridge.pid if bridge is not None else None,
                "last_error": bridge.last_error if bridge is not None else None,
                "scope": "RT physics model + generated codec; not full RT tasks",
            },
            "sys_sil": {
                "state": "unavailable",
                "configured": False,
                "available": False,
                "reason": "SYS is covered by offline tests but is not integrated into the managed live SIL bridge",
            },
            "protocol": {
                "state": "loaded",
                "wire_hash": proto.WIRE_HASH,
            },
        }

    def start_native_sil(self) -> dict:
        """Start the optional RT SIL peer while retaining the virtual transport."""
        if self.sessions.active_profile() is not Profile.PURE_SOFTWARE:
            from control_toolkit.services.session_manager import SessionError

            raise SessionError(
                "simulation.computer_only",
                "software simulation is available only in Computer mode",
                status=409,
            )
        if not isinstance(self.transport, VirtualTransportAdapter):
            from control_toolkit.services.session_manager import SessionError

            raise SessionError(
                "simulation.virtual_transport_required",
                "start the Computer virtual transport before simulation",
                status=409,
            )
        if self.native_sil is not None and self.native_sil.running:
            return self.simulation_status()
        executable = self.config.native_sil_executable
        if not executable:
            from control_toolkit.services.session_manager import SessionError

            raise SessionError(
                "simulation.not_configured",
                "RT SIL executable is not configured (set CTK_NATIVE_SIL_EXE)",
                status=503,
            )
        bridge = NativeSilBridge(executable, self.transport, on_error=self._native_sil_error)
        try:
            bridge.start()
        except Exception as exc:
            bridge.last_error = str(exc)
            self.native_sil = bridge
            self._native_sil_error(str(exc))
            from control_toolkit.services.session_manager import SessionError

            raise SessionError("simulation.start_failed", str(exc), status=503) from exc
        self.native_sil = bridge
        self.diagnostics.emit(
            code="simulation.native_sil_open",
            title="Native RT SIL connected",
            detail=str(bridge.executable),
            severity="info",
        )
        return self.simulation_status()

    def stop_native_sil(self) -> dict:
        """Stop the RT SIL process without closing Computer virtual CAN."""
        if self.sessions.active_profile() is not Profile.PURE_SOFTWARE:
            from control_toolkit.services.session_manager import SessionError

            raise SessionError(
                "simulation.computer_only",
                "software simulation is available only in Computer mode",
                status=409,
            )
        if self.native_sil is not None:
            self.native_sil.stop()
            self.native_sil = None
            self.diagnostics.emit(
                code="simulation.native_sil_stopped",
                title="Native RT SIL stopped",
                detail="Virtual CAN remains open",
                severity="info",
            )
        return self.simulation_status()

    def open_physical_transport(self) -> None:
        """Open CANalyst-II High+Low. Raises if device missing (no silent virtual)."""
        if isinstance(self.transport, CanalystTransportAdapter):
            return
        # Open the candidate before replacing the current virtual transport.
        # A driver/open failure therefore leaves Computer mode intact and is
        # still returned explicitly to the caller (never a silent fallback).
        candidate = CanalystTransportAdapter(
            rx_queue_maxsize=self.config.rx_queue_maxsize,
            bitrate=self.config.canalyst_bitrate,
            device_index=self.config.canalyst_device_index,
            poll_ms=self.config.canalyst_poll_ms,
            receive_timeout_ms=self.config.canalyst_receive_timeout_ms,
            reconnect_initial_ms=self.config.canalyst_reconnect_initial_ms,
            reconnect_max_ms=self.config.canalyst_reconnect_max_ms,
            recovery_stability_ms=self.config.canalyst_recovery_stability_ms,
            on_failure=self._physical_failure_from_worker,
            on_recovered=self._physical_recovered_from_worker,
        )
        candidate.open()
        self._tear_down_transport()
        self.transport = candidate
        self._start_router()
        self.diagnostics.emit(
            code="transport.canalyst_open",
            title="CANalyst-II open",
            detail="CH0=High CH1=Low @ 500 kbit/s",
            severity="info",
        )

    def _physical_failure_from_worker(self, reason: str) -> None:
        loop = self._loop
        if loop is not None and loop.is_running():
            loop.call_soon_threadsafe(self._handle_physical_failure, reason)
        else:
            self._handle_physical_failure(reason)

    def _handle_physical_failure(self, reason: str) -> None:
        self.sessions.transport_failed(reason)
        self.recording.mark_degraded(f"physical transport failure: {reason}")
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

    def _tear_down_transport(self) -> None:
        if self.native_sil is not None:
            self.native_sil.stop()
            self.native_sil = None
        if self.router is not None:
            self.router.stop()
            self.router = None
        if self.transport is not None:
            try:
                self.transport.close()
            except Exception:
                pass
            self.transport = None

    def _on_profile_change(self, profile: Profile) -> None:
        """Switch transport for profile — never silently map physical→virtual."""
        try:
            if profile is Profile.PURE_SOFTWARE:
                self.open_virtual_transport()
            elif profile in (Profile.FULL_VEHICLE, Profile.BENCH_TEST):
                self.open_physical_transport()
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

        # Prefer explicit env transport; else open virtual for Pure Software default.
        import os

        force = (os.getenv("CTK_TRANSPORT") or "").strip().lower()
        if force in ("canalyst", "canalystii", "physical"):
            try:
                self.open_physical_transport()
            except Exception as exc:  # noqa: BLE001
                self.diagnostics.emit(
                    code="transport.canalyst_failed",
                    title="CANalyst open failed",
                    detail=str(exc),
                    severity="error",
                )
                # Do not fall back silently when physical was requested.
                raise
        elif self.config.default_profile is Profile.PURE_SOFTWARE:
            self.open_virtual_transport()
        elif self.config.default_profile in (
            Profile.FULL_VEHICLE,
            Profile.BENCH_TEST,
        ):
            self.open_physical_transport()

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
        if self.native_sil is not None:
            self.native_sil.stop()
            self.native_sil = None
        try:
            self.sessions.close()
        except Exception:
            pass
        self.scheduler.stop()
        self.synthetic.stop_all()
        self.ownership.clear()
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
