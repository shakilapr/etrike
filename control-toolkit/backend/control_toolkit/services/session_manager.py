"""Session lifecycle: profiles, FSM, Bench TX, revision, Stop All (Phase 3)."""

from __future__ import annotations

import threading
import uuid
from typing import TYPE_CHECKING, Callable

from control_toolkit import protocol_bridge as proto
from control_toolkit.config import Profile
from control_toolkit.models.session import (
    BenchTxState,
    ChangeProfileRequest,
    CreateSessionRequest,
    SessionPhase,
    SessionState,
)

if TYPE_CHECKING:
    from control_toolkit.services.ownership import OwnershipTable
    from control_toolkit.services.scheduler import Scheduler


class SessionError(Exception):
    def __init__(self, code: str, detail: str, status: int = 400) -> None:
        super().__init__(detail)
        self.code = code
        self.detail = detail
        self.status = status


TERMINAL_PHASES = frozenset(
    {
        SessionPhase.STOPPED,
        SessionPhase.COMPLETED,
        SessionPhase.FAILED,
        SessionPhase.INCONCLUSIVE,
    }
)

PHYSICAL_PROFILES = frozenset({Profile.FULL_VEHICLE, Profile.BENCH_TEST})


class SessionManager:
    def __init__(
        self,
        *,
        ownership: OwnershipTable,
        get_transport_open: Callable[[], bool],
        get_adapter_epoch: Callable[[], int | None],
        on_stop_all: Callable[[], None] | None = None,
        on_profile_change: Callable[[Profile], None] | None = None,
        physical_available: Callable[[], tuple[bool, str]] | None = None,
    ) -> None:
        self._lock = threading.Lock()
        self._ownership = ownership
        self._get_transport_open = get_transport_open
        self._get_adapter_epoch = get_adapter_epoch
        self._on_stop_all = on_stop_all
        self._on_profile_change = on_profile_change
        self._physical_available = physical_available
        self._state = SessionState(
            wire_hash=proto.WIRE_HASH,
            semantic_hash=proto.SEMANTIC_HASH,
            destination="virtual",
        )
        self._scheduler: Scheduler | None = None

    def bind_scheduler(self, scheduler: Scheduler) -> None:
        self._scheduler = scheduler

    def snapshot(self) -> SessionState:
        with self._lock:
            return self._snapshot_locked()

    def _snapshot_locked(self) -> SessionState:
        st = self._state.model_copy(deep=True)
        st.leases = self._ownership.list_ids()
        if self._scheduler is not None:
            st.jobs = self._scheduler.job_ids()
        return st

    def create(self, req: CreateSessionRequest) -> SessionState:
        with self._lock:
            if self._state.session_id is not None and self._state.phase not in TERMINAL_PHASES:
                raise SessionError(
                    "session.active",
                    "close the active session before creating another",
                    status=409,
                )
            self._assert_profile_allowed_locked(req.profile)

            # Controlled start: Preparing → Listening → Running
            sid = f"ses_{uuid.uuid4().hex[:12]}"
            test_id = req.test_session_id or f"test_{uuid.uuid4().hex[:10]}"
            self._state = SessionState(
                profile=req.profile,
                phase=SessionPhase.PREPARING,
                bench_tx=BenchTxState.DISABLED,
                session_id=sid,
                test_session_id=test_id,
                revision=1,
                adapter_epoch=self._get_adapter_epoch(),
                wire_hash=proto.WIRE_HASH,
                semantic_hash=proto.SEMANTIC_HASH,
                destination=self._destination_for(req.profile),
                capabilities=list(req.capabilities),
            )
        # Transport switch outside lock (may open CANalyst / virtual).
        # Pure Software is usually already open from lifecycle startup — still call
        # so physical→virtual switches work; open_* methods are idempotent.
        if self._on_profile_change is not None:
            try:
                self._on_profile_change(req.profile)
            except Exception as exc:
                # Roll back session if transport cannot open.
                with self._lock:
                    self._state.phase = SessionPhase.FAILED
                    self._state.session_id = None
                    self._state.revision += 1
                raise SessionError(
                    "transport.open_failed",
                    str(exc),
                    status=503,
                ) from exc
        with self._lock:
            # Only report Listening/Running after the selected destination is
            # actually open.  This also records the new physical adapter epoch.
            self._state.adapter_epoch = self._get_adapter_epoch()
            self._state.phase = SessionPhase.LISTENING
            self._state.revision += 1
            self._state.phase = SessionPhase.RUNNING
            self._state.revision += 1
            return self._snapshot_locked()

    def change_profile(self, req: ChangeProfileRequest) -> SessionState:
        """Controlled profile transition: stop TX → neutral → activate."""
        with self._lock:
            self._require_active_locked()
            self._check_revision_locked(req.expected_revision)
            if not req.confirm:
                raise SessionError(
                    "profile.confirm_required",
                    "set confirm=true after reviewing destination; controlled transition required",
                    status=400,
                )
            if req.profile == self._state.profile:
                return self._snapshot_locked()

            self._assert_profile_allowed_locked(req.profile)
            previous = self._state.model_copy(deep=True)

            # Stop periodic TX and disable Bench TX before switch.
            self._cancel_jobs_locked()
            self._ownership.clear()
            cb = self._on_stop_all
            self._state.bench_tx = BenchTxState.DISABLED
            self._state.phase = SessionPhase.PREPARING
            self._state.profile = req.profile
            self._state.destination = self._destination_for(req.profile)
            self._state.adapter_epoch = self._get_adapter_epoch()
            self._state.revision += 1

        if cb is not None:
            cb()

        if self._on_profile_change is not None:
            try:
                self._on_profile_change(req.profile)
            except Exception as exc:
                with self._lock:
                    # Lifecycle opens a candidate before replacing the active
                    # transport, so a failed physical switch can safely restore
                    # the previous visible profile. TX remains neutralized.
                    self._state = previous
                    self._state.bench_tx = BenchTxState.DISABLED
                    self._state.revision += 1
                raise SessionError(
                    "transport.open_failed",
                    str(exc),
                    status=503,
                ) from exc
        with self._lock:
            self._state.adapter_epoch = self._get_adapter_epoch()
            self._state.phase = SessionPhase.LISTENING
            self._state.revision += 1
            self._state.phase = SessionPhase.RUNNING
            self._state.revision += 1
            return self._snapshot_locked()

    def transport_failed(self, reason: str) -> SessionState:
        """Fail safe on physical adapter loss without ending the session.

        Periodic work, ownership leases, and Bench TX are cleared immediately.
        The adapter may reconnect in receive-only mode; the operator must
        explicitly re-enable Bench TX after inspecting the recovered buses.
        """
        with self._lock:
            if self._state.profile not in PHYSICAL_PROFILES:
                return self._snapshot_locked()
            self._cancel_jobs_locked()
            self._ownership.clear()
            cb = self._on_stop_all
            self._state.bench_tx = BenchTxState.DISABLED
            self._state.capabilities = [
                cap for cap in self._state.capabilities if cap != "transport_recovered"
            ]
            if "transport_failed" not in self._state.capabilities:
                self._state.capabilities.append("transport_failed")
            self._state.revision += 1
            snap = self._snapshot_locked()
        if cb is not None:
            cb()
        return snap

    def transport_recovered(self, adapter_epoch: int) -> SessionState:
        """Record receive-only recovery; deliberately do not restore TX/jobs."""
        with self._lock:
            if self._state.profile not in PHYSICAL_PROFILES:
                return self._snapshot_locked()
            self._state.adapter_epoch = adapter_epoch
            self._state.bench_tx = BenchTxState.DISABLED
            self._state.capabilities = [
                cap for cap in self._state.capabilities if cap != "transport_failed"
            ]
            if "transport_recovered" not in self._state.capabilities:
                self._state.capabilities.append("transport_recovered")
            self._state.revision += 1
            return self._snapshot_locked()

    def set_bench_tx(self, enabled: bool, expected_revision: int | None = None) -> SessionState:
        with self._lock:
            self._require_active_locked()
            self._check_revision_locked(expected_revision)
            if self._state.phase is not SessionPhase.RUNNING:
                raise SessionError(
                    "session.not_running",
                    f"Bench TX only in running phase (now {self._state.phase.value})",
                    status=409,
                )
            if enabled and self._state.profile in PHYSICAL_PROFILES:
                ok = True
                reason = ""
                if self._physical_available is not None:
                    ok, reason = self._physical_available()
                if not ok:
                    raise SessionError(
                        "bench_tx.physical_unavailable",
                        f"physical Bench TX unavailable: {reason or 'no CANalyst'}",
                        status=503,
                    )
            if enabled and not self._get_transport_open():
                raise SessionError(
                    "bench_tx.no_adapter",
                    "cannot enable Bench TX without an open adapter (Real mode may be disconnected)",
                    status=409,
                )
            self._state.bench_tx = (
                BenchTxState.ENABLED if enabled else BenchTxState.DISABLED
            )
            if not enabled:
                self._cancel_jobs_locked()
            self._state.revision += 1
            return self._snapshot_locked()

    def stop_all(self, expected_revision: int | None = None) -> SessionState:
        """Full host neutral: TX off, jobs/leases cleared, host ESTOP latch cleared."""
        with self._lock:
            self._require_active_locked()
            self._check_revision_locked(expected_revision)
            self._cancel_jobs_locked()
            self._ownership.clear()
            cb = self._on_stop_all
            # Keep bench_tx ENABLED until after callback so safe zero frames work.
        if cb is not None:
            cb()
        with self._lock:
            self._state.bench_tx = BenchTxState.DISABLED
            self._cancel_jobs_locked()
            self._state.estop_active = False
            self._state.revision += 1
            return self._snapshot_locked()

    def clear_estop_latch(self) -> SessionState:
        """Clear host-side ESTOP injection latch (does not claim vehicle recovery)."""
        with self._lock:
            self._state.estop_active = False
            self._state.revision += 1
            return self._snapshot_locked()

    def mark_link_absent(self, reason: str = "") -> SessionState:
        """Real profile is active but physical adapter is not open."""
        with self._lock:
            if self._state.profile not in PHYSICAL_PROFILES:
                return self._snapshot_locked()
            self._state.bench_tx = BenchTxState.DISABLED
            self._state.capabilities = [
                c
                for c in self._state.capabilities
                if c not in ("link_connected", "transport_recovered")
            ]
            if "link_absent" not in self._state.capabilities:
                self._state.capabilities.append("link_absent")
            # reason is diagnostic-only; do not stamp transport_failed here
            # (that flag is for mid-session loss via transport_failed()).
            _ = reason
            self._state.revision += 1
            return self._snapshot_locked()

    def mark_link_connected(self, adapter_epoch: int | None = None) -> SessionState:
        """Physical adapter opened while already in a Real profile."""
        with self._lock:
            if self._state.profile not in PHYSICAL_PROFILES:
                return self._snapshot_locked()
            if adapter_epoch is not None:
                self._state.adapter_epoch = adapter_epoch
            self._state.capabilities = [
                c
                for c in self._state.capabilities
                if c not in ("link_absent", "transport_failed")
            ]
            if "link_connected" not in self._state.capabilities:
                self._state.capabilities.append("link_connected")
            self._state.revision += 1
            return self._snapshot_locked()

    def close(
        self,
        expected_revision: int | None = None,
        outcome: SessionPhase = SessionPhase.STOPPED,
    ) -> SessionState:
        with self._lock:
            if self._state.session_id is None:
                return self._snapshot_locked()
            self._check_revision_locked(expected_revision)
            self._state.phase = SessionPhase.STOPPING
            self._cancel_jobs_locked()
            self._ownership.clear()
            cb = self._on_stop_all
            self._state.bench_tx = BenchTxState.DISABLED
            if outcome not in TERMINAL_PHASES:
                outcome = SessionPhase.STOPPED
            prev_profile = self._state.profile
            rev = self._state.revision + 1
            self._state = SessionState(
                profile=prev_profile,
                phase=outcome,
                bench_tx=BenchTxState.DISABLED,
                session_id=None,
                test_session_id=None,
                revision=rev,
                wire_hash=proto.WIRE_HASH,
                semantic_hash=proto.SEMANTIC_HASH,
                destination=self._destination_for(prev_profile),
            )
            snap = self._snapshot_locked()
        if cb is not None:
            cb()
        return snap

    def update_vehicle_view(
        self,
        *,
        requested_mode: str | None = None,
        confirmed_mode: str | None = None,
        requested_power: str | None = None,
        confirmed_power: str | None = None,
        estop_active: bool | None = None,
        recording: bool | None = None,
    ) -> SessionState:
        with self._lock:
            if requested_mode is not None:
                self._state.requested_mode = requested_mode
            if confirmed_mode is not None:
                self._state.confirmed_mode = confirmed_mode
            if requested_power is not None:
                self._state.requested_power = requested_power
            if confirmed_power is not None:
                self._state.confirmed_power = confirmed_power
            if estop_active is not None:
                self._state.estop_active = estop_active
            if recording is not None:
                self._state.recording = recording
            return self._snapshot_locked()

    def require_bench_tx_enabled(self) -> None:
        with self._lock:
            if self._state.session_id is None:
                raise SessionError("session.none", "no active session", status=409)
            if self._state.phase is not SessionPhase.RUNNING:
                raise SessionError(
                    "session.not_running",
                    "session is not running",
                    status=409,
                )
            if self._state.bench_tx is not BenchTxState.ENABLED:
                raise SessionError(
                    "bench_tx.disabled",
                    "enable Bench TX before transmitting",
                    status=409,
                )

    def active_profile(self) -> Profile:
        with self._lock:
            return self._state.profile

    def bench_tx(self) -> BenchTxState:
        with self._lock:
            return self._state.bench_tx

    def session_id(self) -> str | None:
        with self._lock:
            return self._state.session_id

    def phase(self) -> SessionPhase:
        with self._lock:
            return self._state.phase

    def _assert_profile_allowed_locked(self, profile: Profile) -> None:
        """Profiles are always selectable.

        Real (bench/full_vehicle) may be entered without a CANalyst so the UI can
        show disconnected state. Physical TX remains gated by open adapter health.
        Never silently map physical → virtual traffic.
        """
        if profile is Profile.PURE_SOFTWARE:
            return
        if profile in PHYSICAL_PROFILES:
            return
        raise SessionError("profile.unknown", f"unknown profile {profile}", status=400)

    @staticmethod
    def _destination_for(profile: Profile) -> str:
        return "virtual" if profile is Profile.PURE_SOFTWARE else "physical"

    def _neutralize_locked(self) -> None:
        """Cancel jobs/leases and disarm TX.

        Must not call ``on_stop_all`` while holding the session lock — the
        callback may re-enter via ``bench_tx()`` / TX paths (deadlock).
        Callers that need the stop-all side effects must invoke the callback
        outside the lock (see ``stop_all`` / ``_neutralize``).
        """
        self._state.bench_tx = BenchTxState.DISABLED
        self._cancel_jobs_locked()
        self._ownership.clear()

    def _neutralize(self) -> None:
        """Thread-safe neutralize with stop-all callback outside the lock."""
        with self._lock:
            self._cancel_jobs_locked()
            self._ownership.clear()
            cb = self._on_stop_all
            # leave TX armed for zero frame if currently enabled
            armed = self._state.bench_tx is BenchTxState.ENABLED
        if cb is not None and armed:
            cb()
        elif cb is not None:
            cb()
        with self._lock:
            self._state.bench_tx = BenchTxState.DISABLED
            self._cancel_jobs_locked()

    def _cancel_jobs_locked(self) -> None:
        if self._scheduler is not None:
            self._scheduler.cancel_all()

    def _require_active_locked(self) -> None:
        if self._state.session_id is None or self._state.phase in TERMINAL_PHASES:
            raise SessionError("session.none", "no active session", status=409)

    def _check_revision_locked(self, expected: int | None) -> None:
        if expected is not None and expected != self._state.revision:
            raise SessionError(
                "session.revision_conflict",
                f"expected revision {expected}, current {self._state.revision}",
                status=409,
            )
