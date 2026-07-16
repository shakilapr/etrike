"""Keyboard/gamepad intent → shaped HOST_DRIVE_CMD (Phase 7, virtual).

Limits and stale timeout mirror firmware shared_config / host.yaml:
  speed_mmps [-500, 3000], yaw_rate_mrad_s [-3000, 3000]
  gear 0=N 1=D 2=S 3=R
  host command stale ~500 ms (shared::kHostCmdStaleTimeoutMs)
  nominal HOST_DRIVE_CMD cycle 10 ms (protocol cycle_ms)
"""

from __future__ import annotations

import threading
import time
from dataclasses import dataclass, field
from typing import Any, Callable

from control_toolkit.models.frames import FrameSource
from control_toolkit.services.scheduler import Scheduler
from control_toolkit.services.session_manager import SessionError
from control_toolkit.services.tx_gate import TxGate

# Firmware-aligned constants (shared_config.h + host.yaml)
MAX_SPEED_FWD_MMPS = 3000
MAX_SPEED_REV_MMPS = 500
MAX_YAW_MRAD_S = 3000
HOST_CMD_STALE_S = 0.5
HOST_DRIVE_PERIOD_MS = 10.0
DEADBAND = 0.05
GEAR_N, GEAR_D, GEAR_S, GEAR_R = 0, 1, 2, 3


@dataclass
class IntentState:
    sequence: int = 0
    source: str = "none"
    mode: str = "none"  # none | kinematics | direct
    throttle: float = 0.0
    steer: float = 0.0
    gear: int = GEAR_N
    hard_brake: bool = False
    estop: bool = False
    last_mono: float = 0.0
    job_id: str | None = None
    lease_owner: str = "control:keyboard"
    shaped_speed: int = 0
    shaped_yaw: int = 0
    active: bool = False
    loss_reason: str | None = None


class ControlIntentService:
    def __init__(
        self,
        *,
        tx_gate: TxGate,
        scheduler: Scheduler,
        require_bench_tx: Callable[[], None],
        on_estop: Callable[[], None] | None = None,
    ) -> None:
        self._tx = tx_gate
        self._scheduler = scheduler
        self._require_bench_tx = require_bench_tx
        self._on_estop = on_estop
        self._lock = threading.Lock()
        self._state = IntentState()
        self._watch_running = False

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            s = self._state
            age = (time.monotonic() - s.last_mono) if s.last_mono else None
            return {
                "active": s.active,
                "mode": s.mode,
                "source": s.source,
                "sequence": s.sequence,
                "throttle": s.throttle,
                "steer": s.steer,
                "gear": s.gear,
                "gear_label": {0: "N", 1: "D", 2: "S", 3: "R"}.get(s.gear, "?"),
                "hard_brake": s.hard_brake,
                "estop": s.estop,
                "shaped_speed_mmps": s.shaped_speed,
                "shaped_yaw_mrad_s": s.shaped_yaw,
                "command_age_s": age,
                "stale_timeout_s": HOST_CMD_STALE_S,
                "job_id": s.job_id,
                "loss_reason": s.loss_reason,
            }

    def apply_intent(
        self,
        *,
        sequence: int,
        source: str = "keyboard",
        mode: str = "kinematics",
        throttle: float = 0.0,
        steer: float = 0.0,
        gear: int | None = None,
        hard_brake: bool = False,
        estop: bool = False,
    ) -> dict[str, Any]:
        self._require_bench_tx()
        now = time.monotonic()
        with self._lock:
            st = self._state
            if st.active and sequence < st.sequence:
                raise SessionError(
                    "control.stale_sequence",
                    f"intent sequence {sequence} < current {st.sequence}",
                    status=409,
                )
            st.sequence = sequence
            st.source = source
            st.mode = mode
            st.throttle = _clamp(float(throttle), -1.0, 1.0)
            st.steer = _clamp(float(steer), -1.0, 1.0)
            if gear is not None:
                st.gear = int(gear) if 0 <= int(gear) <= 3 else GEAR_N
            st.hard_brake = bool(hard_brake)
            st.estop = bool(estop)
            st.last_mono = now
            st.active = True
            st.loss_reason = None

            if st.estop:
                self._zero_locked()
                self._cancel_job_locked()
                # ESTOP bypasses motion ownership — caller injects frames.
                if self._on_estop is not None:
                    pass
                return self._snap_unlocked()

            if mode != "kinematics":
                raise SessionError(
                    "control.mode_unsupported",
                    f"mode {mode!r} not implemented (use kinematics)",
                    status=400,
                )

            speed, yaw, g = self._shape_locked()
            st.shaped_speed = speed
            st.shaped_yaw = yaw
            st.gear = g
            self._ensure_job_locked(speed, yaw, g)
            return self._snap_unlocked()

    def release(self, reason: str = "client_release") -> dict[str, Any]:
        with self._lock:
            self._zero_locked()
            self._cancel_job_locked()
            self._state.active = False
            self._state.mode = "none"
            self._state.loss_reason = reason
            self._state.throttle = 0.0
            self._state.steer = 0.0
            return self._snap_unlocked()

    def tick_watchdog(self) -> None:
        """Called periodically: stop TX if intent stale (firmware 500 ms host stale)."""
        with self._lock:
            if not self._state.active:
                return
            if time.monotonic() - self._state.last_mono > HOST_CMD_STALE_S:
                self._zero_locked()
                self._cancel_job_locked()
                self._state.active = False
                self._state.mode = "none"
                self._state.loss_reason = "stale_intent"
                # Send one zero host-drive so RT would see end sequence on virtual bus
                self._tx.submit(
                    bus="high",
                    key="host:host_drive_cmd",
                    values={
                        "speed_mmps": 0,
                        "yaw_rate_mrad_s": 0,
                        "gear": GEAR_N,
                    },
                    owner=self._state.lease_owner,
                    source=FrameSource.INJECTION,
                    claim_ownership=True,
                    lease_ttl_s=1.0,
                )

    def _shape_locked(self) -> tuple[int, int, int]:
        st = self._state
        thr = st.throttle
        ste = st.steer
        if abs(thr) < DEADBAND:
            thr = 0.0
        if abs(ste) < DEADBAND:
            ste = 0.0
        if st.hard_brake:
            thr = 0.0
            gear = GEAR_N
        else:
            gear = st.gear
            if thr > 0 and gear == GEAR_N:
                gear = GEAR_D
            if thr < 0 and gear not in (GEAR_R,):
                # Reverse intent maps to R; magnitude uses reverse limit
                gear = GEAR_R

        if thr >= 0:
            speed = int(round(thr * MAX_SPEED_FWD_MMPS))
        else:
            speed = int(round(thr * MAX_SPEED_REV_MMPS))  # negative
        if gear == GEAR_R and speed > 0:
            speed = -abs(speed)
        if gear == GEAR_R and speed == 0 and thr < 0:
            speed = int(round(thr * MAX_SPEED_REV_MMPS))

        yaw = int(round(ste * MAX_YAW_MRAD_S))
        speed = int(_clamp(speed, -MAX_SPEED_REV_MMPS, MAX_SPEED_FWD_MMPS))
        yaw = int(_clamp(yaw, -MAX_YAW_MRAD_S, MAX_YAW_MRAD_S))
        if gear == GEAR_S and speed > 0:
            speed = min(int(speed * 1.2), MAX_SPEED_FWD_MMPS)
        return speed, yaw, gear

    def _ensure_job_locked(self, speed: int, yaw: int, gear: int) -> None:
        values = {
            "speed_mmps": speed,
            "yaw_rate_mrad_s": yaw,
            "gear": gear,
        }
        if self._state.job_id and self._scheduler.update_values(
            self._state.job_id, values
        ):
            return
        if self._state.job_id:
            self._scheduler.cancel(self._state.job_id)
            self._state.job_id = None
        self._state.job_id = self._scheduler.schedule(
            bus="high",
            key="host:host_drive_cmd",
            values=values,
            period_ms=HOST_DRIVE_PERIOD_MS,
            owner=self._state.lease_owner,
            source=FrameSource.INJECTION,
        )

    def _cancel_job_locked(self) -> None:
        if self._state.job_id:
            self._scheduler.cancel(self._state.job_id)
            self._state.job_id = None

    def _zero_locked(self) -> None:
        self._state.shaped_speed = 0
        self._state.shaped_yaw = 0

    def _snap_unlocked(self) -> dict[str, Any]:
        s = self._state
        age = (time.monotonic() - s.last_mono) if s.last_mono else None
        return {
            "active": s.active,
            "mode": s.mode,
            "source": s.source,
            "sequence": s.sequence,
            "throttle": s.throttle,
            "steer": s.steer,
            "gear": s.gear,
            "gear_label": {0: "N", 1: "D", 2: "S", 3: "R"}.get(s.gear, "?"),
            "hard_brake": s.hard_brake,
            "estop": s.estop,
            "shaped_speed_mmps": s.shaped_speed,
            "shaped_yaw_mrad_s": s.shaped_yaw,
            "command_age_s": age,
            "stale_timeout_s": HOST_CMD_STALE_S,
            "job_id": s.job_id,
            "loss_reason": s.loss_reason,
        }


def _clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))
