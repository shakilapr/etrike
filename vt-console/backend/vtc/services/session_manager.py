"""Session lifecycle: profiles, FSM, Bench TX, revision, Stop All (workplan §3).

Physical profiles (Full Vehicle, Bench Test) are refused outright — Phase 2
(CANalyst-II transport) is deferred, so there is no physical adapter to open.
This is deliberate: an adapter-loss or "not built yet" condition must never
silently fall back to Pure Software (architecture requirement, workplan §3.1).
"""

from __future__ import annotations

import threading
import uuid
from typing import Callable

from vtc import protocol_bridge as proto
from vtc.config import Profile
from vtc.models.session import (
    BenchTxState,
    ChangeProfileRequest,
    CreateSessionRequest,
    SessionPhase,
    SessionState,
)
from vtc.services.ownership import OwnershipTable


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
    ) -> None:
        self._lock = threading.Lock()
        self._ownership = ownership
        self._get_transport_open = get_transport_open
        self._get_adapter_epoch = get_adapter_epoch
        self._on_stop_all = on_stop_all
        self._state = SessionState(wire_hash=proto.WIRE_HASH, destination="virtual")

    def snapshot(self) -> SessionState:
        with self._lock:
            return self._snapshot_locked()

    def _snapshot_locked(self) -> SessionState:
        st = self._state.model_copy(deep=True)
        st.leases = self._ownership.list_ids()
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

            # Controlled start: Preparing -> Listening -> Running.
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
                destination=self._destination_for(req.profile),
                capabilities=list(req.capabilities),
            )
            self._state.phase = SessionPhase.LISTENING  # observe bus before TX
            self._state.revision += 1
            self._state.phase = SessionPhase.RUNNING  # active for commands
            self._state.revision += 1
            return self._snapshot_locked()

    def change_profile(self, req: ChangeProfileRequest) -> SessionState:
        """Controlled profile transition: stop TX -> neutral -> activate (§3.1)."""
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

            self._neutralize_locked()
            self._state.phase = SessionPhase.PREPARING
            self._state.profile = req.profile
            self._state.destination = self._destination_for(req.profile)
            self._state.adapter_epoch = self._get_adapter_epoch()
            self._state.revision += 1

            self._state.phase = SessionPhase.LISTENING
            self._state.revision += 1
            self._state.phase = SessionPhase.RUNNING
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
                raise SessionError(
                    "bench_tx.physical_unavailable",
                    "physical Bench TX is not available yet (Phase 2 deferred)",
                    status=503,
                )
            if enabled and not self._get_transport_open():
                raise SessionError(
                    "bench_tx.no_adapter",
                    "cannot enable Bench TX without an open adapter",
                    status=409,
                )
            self._state.bench_tx = BenchTxState.ENABLED if enabled else BenchTxState.DISABLED
            self._state.revision += 1
            return self._snapshot_locked()

    def stop_all(self, expected_revision: int | None = None) -> SessionState:
        with self._lock:
            self._require_active_locked()
            self._check_revision_locked(expected_revision)
            self._neutralize_locked()
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
            self._neutralize_locked()
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
                destination=self._destination_for(prev_profile),
            )
            return self._snapshot_locked()

    def require_bench_tx_enabled(self) -> None:
        with self._lock:
            if self._state.session_id is None:
                raise SessionError("session.none", "no active session", status=409)
            if self._state.phase is not SessionPhase.RUNNING:
                raise SessionError("session.not_running", "session is not running", status=409)
            if self._state.bench_tx is not BenchTxState.ENABLED:
                raise SessionError(
                    "bench_tx.disabled", "enable Bench TX before transmitting", status=409
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
        if profile is Profile.PURE_SOFTWARE:
            if not self._get_transport_open():
                raise SessionError(
                    "transport.unavailable",
                    "Pure Software requires an open virtual transport",
                    status=503,
                )
            return
        if profile in PHYSICAL_PROFILES:
            # Never silent virtual fallback — refuse outright.
            raise SessionError(
                "profile.physical_unavailable",
                f"{profile.value} requires a physical adapter (Phase 2 deferred)",
                status=503,
            )
        raise SessionError("profile.unknown", f"unknown profile {profile}", status=400)

    @staticmethod
    def _destination_for(profile: Profile) -> str:
        return "virtual" if profile is Profile.PURE_SOFTWARE else "physical"

    def _neutralize_locked(self) -> None:
        self._state.bench_tx = BenchTxState.DISABLED
        self._ownership.clear()
        if self._on_stop_all is not None:
            self._on_stop_all()

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
