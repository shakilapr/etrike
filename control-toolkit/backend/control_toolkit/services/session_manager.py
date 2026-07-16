"""Session lifecycle: profiles, Bench TX, revision, Stop All."""

from __future__ import annotations

import threading
import uuid
from typing import TYPE_CHECKING, Callable

from control_toolkit import protocol_bridge as proto
from control_toolkit.config import Profile
from control_toolkit.models.session import (
    BenchTxState,
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
        self._state = SessionState(wire_hash=proto.WIRE_HASH)
        self._scheduler: Scheduler | None = None

    def bind_scheduler(self, scheduler: Scheduler) -> None:
        self._scheduler = scheduler

    def snapshot(self) -> SessionState:
        with self._lock:
            st = self._state.model_copy(deep=True)
            st.leases = self._ownership.list_ids()
            if self._scheduler is not None:
                st.jobs = self._scheduler.job_ids()
            return st

    def create(self, req: CreateSessionRequest) -> SessionState:
        with self._lock:
            if self._state.session_id is not None and self._state.phase not in (
                SessionPhase.STOPPED,
                SessionPhase.COMPLETED,
                SessionPhase.FAILED,
            ):
                raise SessionError(
                    "session.active",
                    "close the active session before creating another",
                    status=409,
                )

            if req.profile is Profile.PURE_SOFTWARE:
                if not self._get_transport_open():
                    raise SessionError(
                        "transport.unavailable",
                        "Pure Software requires an open virtual transport",
                        status=503,
                    )
            else:
                # No silent virtual fallback for physical profiles.
                raise SessionError(
                    "profile.physical_unavailable",
                    f"{req.profile.value} requires a physical adapter (not available yet)",
                    status=503,
                )

            self._state = SessionState(
                profile=req.profile,
                phase=SessionPhase.RUNNING,
                bench_tx=BenchTxState.DISABLED,
                session_id=f"ses_{uuid.uuid4().hex[:12]}",
                revision=1,
                adapter_epoch=self._get_adapter_epoch(),
                wire_hash=proto.WIRE_HASH,
            )
            return self._state.model_copy(deep=True)

    def set_bench_tx(self, enabled: bool, expected_revision: int | None = None) -> SessionState:
        with self._lock:
            self._require_active_locked()
            self._check_revision_locked(expected_revision)
            if enabled and self._state.profile is not Profile.PURE_SOFTWARE:
                raise SessionError(
                    "bench_tx.physical_unavailable",
                    "physical Bench TX is not available yet",
                    status=503,
                )
            if enabled and not self._get_transport_open():
                raise SessionError(
                    "bench_tx.no_adapter",
                    "cannot enable Bench TX without an open adapter",
                    status=409,
                )
            self._state.bench_tx = (
                BenchTxState.ENABLED if enabled else BenchTxState.DISABLED
            )
            if not enabled and self._scheduler is not None:
                self._scheduler.cancel_all()
            self._state.revision += 1
            return self._state.model_copy(deep=True)

    def stop_all(self, expected_revision: int | None = None) -> SessionState:
        with self._lock:
            self._require_active_locked()
            self._check_revision_locked(expected_revision)
            self._state.bench_tx = BenchTxState.DISABLED
            if self._scheduler is not None:
                self._scheduler.cancel_all()
            self._ownership.clear()
            if self._on_stop_all is not None:
                self._on_stop_all()
            self._state.revision += 1
            return self._state.model_copy(deep=True)

    def close(self, expected_revision: int | None = None) -> SessionState:
        with self._lock:
            if self._state.session_id is None:
                return self._state.model_copy(deep=True)
            self._check_revision_locked(expected_revision)
            if self._scheduler is not None:
                self._scheduler.cancel_all()
            self._ownership.clear()
            if self._on_stop_all is not None:
                self._on_stop_all()
            self._state = SessionState(
                profile=self._state.profile,
                phase=SessionPhase.STOPPED,
                bench_tx=BenchTxState.DISABLED,
                session_id=None,
                revision=self._state.revision + 1,
                wire_hash=proto.WIRE_HASH,
            )
            return self._state.model_copy(deep=True)

    def require_bench_tx_enabled(self) -> None:
        with self._lock:
            if self._state.session_id is None:
                raise SessionError("session.none", "no active session", status=409)
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

    def _require_active_locked(self) -> None:
        if self._state.session_id is None:
            raise SessionError("session.none", "no active session", status=409)

    def _check_revision_locked(self, expected: int | None) -> None:
        if expected is not None and expected != self._state.revision:
            raise SessionError(
                "session.revision_conflict",
                f"expected revision {expected}, current {self._state.revision}",
                status=409,
            )
