"""HMI service: session management and control orchestration (workplan §5.7)."""

from __future__ import annotations

import asyncio
import uuid
from dataclasses import dataclass, field
from datetime import datetime
from typing import Any

from vtc.config import Profile
from vtc.models.session import BenchTxState, SessionState
from vtc.services.injections import InjectionService
from vtc.services.source_conflict_monitor import SourceConflictMonitor
from vtc.services.synthetic_peers import DutKind, SyntheticPeerEngine


@dataclass
class HmiSession:
    """HMI session record."""

    session_id: str
    name: str
    profile: str  # "pure_software", "bench_test", "full_vehicle"
    dut: str  # "sys" or "rt"
    bench_tx_enabled: bool = False
    created_at: datetime = field(default_factory=datetime.utcnow)
    updated_at: datetime = field(default_factory=datetime.utcnow)
    state: SessionState | None = None  # Linked session state


class HmiService:
    """HMI service for session management and control.

    Responsibilities:
    - Manage session lifecycle
    - Coordinate synthetic peers, injections, and monitoring
    - Provide high-level control operations
    - Track bench testing state
    """

    def __init__(
        self,
        synthetic_peers: SyntheticPeerEngine,
        conflict_monitor: SourceConflictMonitor,
        injection_service: InjectionService,
    ):
        """Initialize HMI service.

        Args:
            synthetic_peers: Synthetic peer engine
            conflict_monitor: Source conflict monitor
            injection_service: Injection service
        """
        self.synthetic_peers = synthetic_peers
        self.conflict_monitor = conflict_monitor
        self.injection_service = injection_service

        # Session storage
        self._lock = asyncio.Lock()
        self.sessions: dict[str, HmiSession] = {}
        self.max_sessions = 100

    async def create_session(
        self,
        name: str,
        profile: str = "pure_software",
        dut: str = "sys",
    ) -> HmiSession:
        """Create a new HMI session.

        Args:
            name: Session name
            profile: Operating profile
            dut: Device-under-test

        Returns:
            HmiSession

        Raises:
            ValueError: Invalid profile or DUT
        """
        # Validate
        valid_profiles = ["pure_software", "bench_test", "full_vehicle"]
        if profile not in valid_profiles:
            raise ValueError(f"Invalid profile: {profile}")

        if dut not in ("sys", "rt"):
            raise ValueError(f"Invalid DUT: {dut}")

        async with self._lock:
            if len(self.sessions) >= self.max_sessions:
                raise ValueError("Max sessions reached")

            # Create session
            session_id = f"ses_{uuid.uuid4().hex[:12]}"
            session = HmiSession(
                session_id=session_id,
                name=name,
                profile=profile,
                dut=dut,
                bench_tx_enabled=False,
            )

            # Create linked SessionState (for TX Gate integration)
            session.state = SessionState()
            session.state.session_id = session_id
            session.state.profile = Profile[profile.upper()]
            session.state.bench_tx = BenchTxState.DISABLED

            self.sessions[session_id] = session
            return session

    async def get_session(self, session_id: str) -> HmiSession | None:
        """Get session by ID.

        Args:
            session_id: Session ID

        Returns:
            HmiSession or None
        """
        async with self._lock:
            return self.sessions.get(session_id)

    async def list_sessions(self) -> list[HmiSession]:
        """List all sessions.

        Returns:
            List of HmiSessions
        """
        async with self._lock:
            return list(self.sessions.values())

    async def update_session(
        self,
        session_id: str,
        profile: str | None = None,
        bench_tx_enabled: bool | None = None,
    ) -> HmiSession | None:
        """Update session settings.

        Args:
            session_id: Session ID
            profile: New profile (optional)
            bench_tx_enabled: Enable/disable Bench TX (optional)

        Returns:
            Updated HmiSession or None if not found
        """
        async with self._lock:
            session = self.sessions.get(session_id)
            if not session:
                return None

            if profile:
                valid_profiles = ["pure_software", "bench_test", "full_vehicle"]
                if profile not in valid_profiles:
                    raise ValueError(f"Invalid profile: {profile}")
                session.profile = profile
                if session.state:
                    session.state.profile = Profile[profile.upper()]

            if bench_tx_enabled is not None:
                session.bench_tx_enabled = bench_tx_enabled
                if session.state:
                    session.state.bench_tx = (
                        BenchTxState.ENABLED
                        if bench_tx_enabled
                        else BenchTxState.DISABLED
                    )

            session.updated_at = datetime.utcnow()
            return session

    async def delete_session(self, session_id: str) -> dict[str, int]:
        """Delete session and clean up resources.

        Args:
            session_id: Session ID

        Returns:
            Dict with cleanup counts
        """
        async with self._lock:
            session = self.sessions.pop(session_id, None)
            if not session:
                return {"peers": 0, "injections": 0}

        # Clean up resources
        peers_stopped = await self.synthetic_peers.stop_all(session_id)
        injections_cleared = await self.injection_service.clear_session_injections(
            session_id
        )
        await self.conflict_monitor.stop_monitoring(session_id)

        return {"peers": peers_stopped, "injections": injections_cleared}

    async def start_bench_test(
        self, session_id: str, dut: str, listen_duration_ms: int = 500
    ) -> dict[str, Any]:
        """Start bench testing workflow.

        Args:
            session_id: Session ID
            dut: Device-under-test
            listen_duration_ms: Listen window duration

        Returns:
            Status dict
        """
        # Get session
        session = await self.get_session(session_id)
        if not session:
            raise ValueError(f"Session not found: {session_id}")

        # Validate DUT
        if dut not in ("sys", "rt"):
            raise ValueError(f"Invalid DUT: {dut}")

        # Enable Bench TX
        await self.update_session(session_id, bench_tx_enabled=True)

        # Start monitoring
        dut_kind = DutKind.SYS if dut == "sys" else DutKind.RT
        await self.conflict_monitor.start_monitoring(session_id, dut_kind)

        # Start listen window
        window = await self.synthetic_peers.start_listen_window(
            session_id, dut_kind, listen_duration_ms
        )

        import time

        remaining = window.remaining_ms(time.monotonic_ns())

        return {
            "status": "listening",
            "listening": True,
            "listening_remaining_ms": remaining,
        }

    async def stop_bench_test(self, session_id: str) -> dict[str, Any]:
        """Stop bench testing workflow.

        Args:
            session_id: Session ID

        Returns:
            Status dict with cleanup counts
        """
        # Stop synthetic peers
        peers_stopped = await self.synthetic_peers.stop_all(session_id)

        # Cancel pending injections
        injections_cleared = await self.injection_service.clear_session_injections(
            session_id
        )

        # Disable Bench TX
        await self.update_session(session_id, bench_tx_enabled=False)

        # Stop monitoring
        await self.conflict_monitor.stop_monitoring(session_id)

        return {
            "status": "stopped",
            "synthetic_peers_stopped": peers_stopped,
            "pending_injections_cancelled": injections_cleared,
        }

    async def get_system_status(self, session_id: str) -> dict[str, Any]:
        """Get overall system status for a session.

        Args:
            session_id: Session ID

        Returns:
            Status dict
        """
        session = await self.get_session(session_id)
        if not session:
            raise ValueError(f"Session not found: {session_id}")

        # Get status from different services
        peers_status = await self.synthetic_peers.get_status()
        conflict_report = await self.conflict_monitor.get_conflict_report()
        injection_stats = await self.injection_service.get_session_stats(session_id)

        return {
            "session_id": session_id,
            "profile": session.profile,
            "dut": session.dut,
            "bench_tx_enabled": session.bench_tx_enabled,
            "synthetic_peers_active": peers_status["active_peer_count"],
            "pending_injections": injection_stats["pending"],
            "submitted_injections": injection_stats["submitted"],
            "total_conflicts": conflict_report["conflict_count"],
            "listening": peers_status["listening"],
        }

    async def get_control_workspace_status(self, session_id: str) -> dict[str, Any]:
        """Get status optimized for control workspace UI.

        Args:
            session_id: Session ID

        Returns:
            Status dict
        """
        system_status = await self.get_system_status(session_id)

        peers_status = await self.synthetic_peers.get_status()
        conflict_report = await self.conflict_monitor.get_conflict_report()
        injection_stats = await self.injection_service.get_session_stats(session_id)

        return {
            "session_id": session_id,
            "profile": system_status["profile"],
            "dut": system_status["dut"],
            "bench_tx_enabled": system_status["bench_tx_enabled"],
            "synthetic_peers": {
                "active_count": peers_status["active_peer_count"],
                "listening": peers_status["listening"],
                "listen_remaining_ms": peers_status["listen_remaining_ms"],
                "peers": peers_status["active_peers"],
            },
            "injections": {
                "pending": injection_stats["pending"],
                "submitted": injection_stats["submitted"],
                "failed": injection_stats["failed"],
                "cancelled": injection_stats["cancelled"],
                "total": injection_stats["total"],
            },
            "conflicts": {
                "total": conflict_report["conflict_count"],
                "recent": conflict_report["recent_conflicts"],
            },
            "system_status": "healthy",
        }
