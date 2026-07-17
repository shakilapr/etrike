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
    # Direct-actuator periodic jobs (low bus). Mutually exclusive with kinematics.
    direct_jobs: dict[str, str] = field(default_factory=dict)


# Vendor SES angle raw: 0.1°/bit-ish i16; speed raw 125–525 (°/s-ish per codec).
# SEB pressure_request_raw: 0–100 (codec limit).
DIRECT_CHANNELS = {
    "motor": {
        "bus": "low",
        "key": "rt:rt_drive_cmd",
        "period_ms": 10.0,
        "owner": "control:direct:motor",
    },
    "steering": {
        "bus": "low",
        "key": "ses:vcu_ses_req",
        "period_ms": 20.0,
        "owner": "control:direct:steering",
    },
    "brake": {
        "bus": "low",
        "key": "seb:vcu_seb_req",
        "period_ms": 20.0,
        "owner": "control:direct:brake",
    },
}


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
            return self._snap_unlocked()

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
            # Stale-sequence guard only applies to the *same* continuous producer
            # (e.g. Drive tab remount must not 409 against Control keyboard seq).
            same_stream = (
                st.active
                and st.mode == "kinematics"
                and st.source == source
                and mode == "kinematics"
            )
            if same_stream and sequence < st.sequence:
                raise SessionError(
                    "control.stale_sequence",
                    f"intent sequence {sequence} < current {st.sequence}",
                    status=409,
                )
            # Mutual exclusion: kinematics preempts direct actuator jobs.
            if mode == "kinematics" and st.direct_jobs:
                self._cancel_direct_locked()
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
                self._cancel_direct_locked()
                if self._on_estop is not None:
                    pass
                return self._snap_unlocked()

            if mode != "kinematics":
                raise SessionError(
                    "control.mode_unsupported",
                    f"mode {mode!r} not for intent (use /control/direct)",
                    status=400,
                )

            speed, yaw, g = self._shape_locked()
            st.shaped_speed = speed
            st.shaped_yaw = yaw
            st.gear = g
            self._ensure_job_locked(speed, yaw, g)
            return self._snap_unlocked()

    def set_direct(
        self,
        *,
        channel: str,
        enabled: bool,
        values: dict[str, Any] | None = None,
        period_ms: float | None = None,
    ) -> dict[str, Any]:
        """Start/stop/update a low-bus actuator stream (SES / SEB / RT_DRIVE)."""
        if channel not in DIRECT_CHANNELS:
            raise SessionError(
                "control.unknown_channel",
                f"channel must be one of {list(DIRECT_CHANNELS)}",
                status=400,
            )
        # Allow stop without Bench TX (cleanup / method switch); start requires it.
        if enabled:
            self._require_bench_tx()
        with self._lock:
            # Mutual exclusion: direct preempts kinematics.
            if enabled and self._state.job_id:
                self._cancel_job_locked()
                self._state.active = False
                self._state.sequence = 0
            self._state.mode = "direct" if enabled else (
                "direct" if self._state.direct_jobs else "none"
            )
            self._state.loss_reason = None
            self._state.last_mono = time.monotonic()
            if enabled:
                self._state.source = "direct"

            spec = DIRECT_CHANNELS[channel]
            existing = self._state.direct_jobs.get(channel)
            if not enabled:
                if existing:
                    self._scheduler.cancel(existing)
                    del self._state.direct_jobs[channel]
                if not self._state.direct_jobs:
                    self._state.mode = "none"
                    self._state.active = False
                return self._snap_unlocked()

            vals = self._normalize_direct_values(channel, values or {})
            if existing and self._scheduler.update_values(existing, vals):
                self._state.active = True
                return self._snap_unlocked()
            if existing:
                self._scheduler.cancel(existing)
            job_id = self._scheduler.schedule(
                bus=spec["bus"],
                key=spec["key"],
                values=vals,
                period_ms=period_ms or float(spec["period_ms"]),
                owner=spec["owner"],
                source=FrameSource.INJECTION,
                counter_field="rolling_counter"
                if channel in ("steering", "brake")
                else None,
            )
            self._state.direct_jobs[channel] = job_id
            self._state.mode = "direct"
            self._state.active = True
            self._state.source = "direct"
            return self._snap_unlocked()

    def release(self, reason: str = "client_release") -> dict[str, Any]:
        with self._lock:
            self._zero_locked()
            self._cancel_job_locked()
            owners = [self._state.lease_owner] + [
                str(DIRECT_CHANNELS[ch]["owner"]) for ch in list(self._state.direct_jobs)
            ]
            # Also drop leases for all known direct owners (may have been stopped already).
            owners.extend(str(spec["owner"]) for spec in DIRECT_CHANNELS.values())
            self._cancel_direct_locked()
            self._state.active = False
            self._state.mode = "none"
            self._state.loss_reason = reason
            self._state.throttle = 0.0
            self._state.steer = 0.0
            self._state.sequence = 0
            self._state.source = "none"
            for owner in dict.fromkeys(owners):
                try:
                    self._tx.release_owner(owner)
                except Exception:  # noqa: BLE001
                    pass
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
            if thr > 0 and gear in (GEAR_N, GEAR_R):
                gear = GEAR_D
            if thr < 0 and gear not in (GEAR_R,):
                # Reverse intent maps to R; magnitude uses reverse limit
                gear = GEAR_R

        if thr >= 0:
            speed = int(round(thr * MAX_SPEED_FWD_MMPS))
        else:
            speed = int(round(thr * MAX_SPEED_REV_MMPS))  # negative

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

    def _cancel_direct_locked(self) -> None:
        for job_id in list(self._state.direct_jobs.values()):
            self._scheduler.cancel(job_id)
        self._state.direct_jobs.clear()

    def _zero_locked(self) -> None:
        self._state.shaped_speed = 0
        self._state.shaped_yaw = 0

    @staticmethod
    def _normalize_direct_values(channel: str, values: dict[str, Any]) -> dict[str, Any]:
        if channel == "motor":
            speed = int(values.get("motor_speed_mmps", values.get("speed_mmps", 0)))
            speed = int(_clamp(speed, -MAX_SPEED_REV_MMPS, MAX_SPEED_FWD_MMPS))
            gear = int(values.get("gear", GEAR_D))
            if gear not in (0, 1, 2, 3):
                gear = GEAR_D
            return {"motor_speed_mmps": speed, "gear": gear}
        if channel == "steering":
            # target_angle_raw: signed 0.1° units, clamp ±450 (45°)
            # Safety bypass for toolkit direct path: control + alignment always ON.
            angle = int(values.get("target_angle_raw", values.get("angle_raw", 0)))
            angle = int(_clamp(angle, -450, 450))
            speed = int(values.get("target_speed_raw", 328))
            speed = int(_clamp(speed, 125, 525))
            return {
                "alignment_enable": True,
                "control_enable": True,
                "target_angle_raw": angle,
                "target_speed_raw": speed,
                "rolling_counter": int(values.get("rolling_counter", 0)) & 0xF,
                "vehicle_speed_raw": int(values.get("vehicle_speed_raw", 0)) & 0xFF,
            }
        if channel == "brake":
            # Safety bypass for toolkit direct path: control + alignment always ON.
            pressure = int(values.get("pressure_request_raw", values.get("pressure", 0)))
            pressure = int(_clamp(pressure, 0, 100))
            stroke = int(values.get("stroke_request_raw", 600))
            stroke = int(_clamp(stroke, 0, 0xFFFF))
            mode = int(values.get("control_mode", 1))  # 1 = pressure mode
            if mode not in (0, 1):
                mode = 1
            return {
                "alignment_enable": True,
                "control_enable": True,
                "auto_brake": bool(values.get("auto_brake", False)),
                "control_mode": mode,
                "stroke_request_raw": stroke,
                "pressure_request_raw": pressure,
                "rolling_counter": int(values.get("rolling_counter", 0)) & 0xF,
            }
        return dict(values)

    def _snap_unlocked(self) -> dict[str, Any]:
        """Full control snapshot; caller must hold ``self._lock``."""
        s = self._state
        age = (time.monotonic() - s.last_mono) if s.last_mono else None
        if s.mode == "kinematics" or s.job_id:
            method = "high_kinematics"
            bus: str | None = "high"
            method_label = "High bus · Host kinematics (HOST_DRIVE_CMD)"
        elif s.mode == "direct" or s.direct_jobs:
            method = "low_direct"
            bus = "low"
            method_label = "Low bus · Direct actuators (motor / steer / brake)"
        else:
            method = "none"
            bus = None
            method_label = "No active motion method"
        return {
            "active": bool(s.active or s.direct_jobs),
            "mode": s.mode,
            "method": method,
            "bus": bus,
            "method_label": method_label,
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
            "direct_channels": list(s.direct_jobs.keys()),
            "direct_jobs": dict(s.direct_jobs),
            "paths": {
                "high_kinematics": {
                    "bus": "high",
                    "message": "HOST_DRIVE_CMD",
                    "can_id": 0x300,
                    "owner_role": "Host intent; RT runs kinematics",
                    "period_ms": HOST_DRIVE_PERIOD_MS,
                    "stale_s": HOST_CMD_STALE_S,
                },
                "low_direct": {
                    "bus": "low",
                    "channels": {
                        ch: {
                            "key": spec["key"],
                            "period_ms": spec["period_ms"],
                        }
                        for ch, spec in DIRECT_CHANNELS.items()
                    },
                    "owner_role": "Bypass RT kinematics; control_enable+alignment forced ON",
                },
            },
        }


def _clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))
