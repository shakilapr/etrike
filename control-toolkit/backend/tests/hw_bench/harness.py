"""Shared harness for the live hardware-bench pytest suite.

Design rules (hardware-bench handoff, 2026-09-15):

* The control-toolkit backend (uvicorn on :8001) is the **sole owner** of the
  CANalyst-II USB adapter. This suite therefore talks to the REST API only and
  never instantiates ``can.Bus`` directly (that would raise ``Errno 13`` on
  Windows while the backend holds the device).
* MTR, SES and SEB are physically absent. This suite **never synthesises
  absent-actuator feedback** (no 0x201 / 0x206 / 0x721 / 0x731 injections).
  It only *observes* the output commands RT/SYS produce for those actuators
  and *injects* legitimate Host/HMI inputs.
* The rig runs ``hardware_bench`` firmware (developer bypass mode), so missing
  peer feedback must not trip a safety stop. That property is asserted
  explicitly (``test_h4_actuator_outputs.test_bypass_mode_holds_without_peers``).

The REST client itself is reused from ``scripts/hardware_bench_suite.py`` so
there is a single in-repo implementation of the bench protocol.
"""

from __future__ import annotations

import sys
import time
from pathlib import Path
from typing import Any, Callable, Optional

_SCRIPTS = Path(__file__).resolve().parents[2] / "scripts"
if str(_SCRIPTS) not in sys.path:  # scripts/ is not a package
    sys.path.insert(0, str(_SCRIPTS))

from hardware_bench_suite import BenchClient  # noqa: E402

# ── Logical buses ────────────────────────────────────────────────────────
HIGH = "high"
LOW = "low"

# ── CAN identities (protocol/generated/python/etrike_protocol.py) ────────
CAN_SAFETY_ESTOP = 0x001
CAN_SYS_SAFETY_STS = 0x011
CAN_SYS_MODE_CMD = 0x110
CAN_SYS_PWR_CMD = 0x113
CAN_ESTOP_RESET_RSP = 0x115
CAN_SES_REQ = 0x169
CAN_RT_DRIVE_CMD = 0x204
CAN_RT_BRAKE_CMD = 0x205
CAN_RT_STATE_RPT = 0x210
CAN_RT_MOTION_RPT = 0x121
CAN_HOST_DRIVE_CMD = 0x300
CAN_HOST_BRAKE_REQ = 0x301
CAN_HOST_LIGHT_CMD = 0x302
CAN_HOST_OBSTACLE_DIST = 0x400
CAN_SYS_NODE_STATUS = 0x500
CAN_RT_NODE_STATUS = 0x501
CAN_SYS_DIAG_RPT = 0x600
CAN_SEB_REQ = 0x7B9
CAN_HOST_HEARTBEAT = 0x7FC
CAN_RT_HEARTBEAT = 0x7FD
CAN_SYS_HEARTBEAT = 0x7FE

# ── Canonical injection keys ─────────────────────────────────────────────
KEY_MODE_REQ = "hmi:hmi_mode_req"          # signal: req_mode      (0=MANUAL, 1=AUTO)
KEY_PWR_REQ = "hmi:hmi_pwr_req"            # signal: req_start     (0=OFF, 1=ON)
KEY_ESTOP_RESET_REQ = "hmi:host_estop_reset_req"  # request_seq + reset_token
KEY_HOST_DRIVE = "host:host_drive_cmd"     # speed_mmps / yaw_rate_mrad_s / gear
KEY_HOST_BRAKE = "host:host_brake_req"     # brake_pressure_kpa
KEY_HOST_LIGHT = "host:host_light_cmd"     # left_turn / right_turn / brake_light / headlight
KEY_HOST_OBSTACLE = "host:host_obstacle_dist"  # distance_mm (0xFFFFFFFF = clear)

# ── Enumerations (generated dictionary) ──────────────────────────────────
NODE_INIT = 0
NODE_STANDBY = 2
NODE_ACTIVE = 3
NODE_ESTOP = 5
NODE_STATES_OPERATIONAL = (NODE_STANDBY, NODE_ACTIVE)

MODE_MANUAL = 0
MODE_AUTO = 1
MODE_ESTOP = 2

GEAR_N = 0
GEAR_D = 1
GEAR_S = 2
GEAR_R = 3

SEB_MODE_STROKE = 0
SEB_MODE_PRESSURE = 1
# sys-esp32/src/brake_control.h: raw = (mm - offset) / scale, offset -30, scale 0.05
SEB_STROKE_RAW_ZERO = 600        # 0 mm released
SEB_STROKE_RAW_ESTOP_MAX = 1140  # 27 mm ESTOP (brake_control.h:103)

ESTOP_RESET_TOKEN = 0x5253
OBSTACLE_CLEAR = 0xFFFFFFFF

# ── Directive guard: absent-actuator feedback must never be injected ─────
FORBIDDEN_MOCK_KEYS = (
    "ses:sbw_status",
    "ses:ses_status",
    "seb:bbw_status",
    "seb:seb_status",
    "mtr:mtr_motor_fbk",
    "mtr:sys_throttle_sts",
)

StateMap = dict[tuple[str, int], dict[str, Any]]


def signal_of(message: Optional[dict], name: str, default: Any = None) -> Any:
    """Return the engineering value of ``name`` from a ``/state`` message."""
    if not message:
        return default
    entry = (message.get("signals") or {}).get(name)
    if entry is None:
        return default
    if isinstance(entry, dict):
        return entry.get("engineering_value", default)
    return entry


def signal_from(state: StateMap, bus: str, can_id: int, name: str, default: Any = None) -> Any:
    return signal_of(state.get((bus, int(can_id))), name, default)


def is_live(message: Optional[dict]) -> bool:
    return bool(message) and str(message.get("freshness")) == "live"


class HwBench(BenchClient):
    """REST-only bench client with wait/assert helpers for the pytest suite."""

    timeout_s: float = 3.0

    def __init__(self, base_url: str = "http://127.0.0.1:8001") -> None:
        super().__init__(base_url=base_url)
        self._hb_ctr: dict[str, int] = {}
        self._supervised = {KEY_MODE_REQ, KEY_PWR_REQ, KEY_ESTOP_RESET_REQ}

    # ── state reads ──────────────────────────────────────────────────────
    def state_map(self) -> StateMap:
        _, data = self.request("GET", "/state")
        out: StateMap = {}
        for m in (data or {}).get("messages", []):
            bus, cid = m.get("bus"), m.get("can_id")
            if bus is None or cid is None:
                continue
            out[(bus, int(cid))] = m
        return out

    def message(self, bus: str, can_id: int) -> Optional[dict]:
        return self.state_map().get((bus, int(can_id)))

    def signal(self, bus: str, can_id: int, name: str, default: Any = None) -> Any:
        return signal_of(self.message(bus, can_id), name, default)

    def rate(self, bus: str, can_id: int) -> float:
        m = self.message(bus, can_id) or {}
        return float(m.get("observed_rate_hz") or 0.0)

    # ── waits ────────────────────────────────────────────────────────────
    def wait_for(
        self,
        predicate: Callable[[StateMap], bool],
        timeout_s: Optional[float] = None,
        interval_s: float = 0.02,
    ) -> tuple[bool, StateMap]:
        timeout_s = self.timeout_s if timeout_s is None else timeout_s
        deadline = time.monotonic() + timeout_s
        state: StateMap = {}
        while True:
            state = self.state_map()
            try:
                if predicate(state):
                    return True, state
            except Exception:  # noqa: BLE001 — a transient decode gap is not a failure
                pass
            if time.monotonic() >= deadline:
                return False, state
            time.sleep(interval_s)

    def wait_signal(
        self,
        bus: str,
        can_id: int,
        name: str,
        expected: Any = None,
        predicate: Optional[Callable[[Any], bool]] = None,
        timeout_s: Optional[float] = None,
    ) -> tuple[bool, StateMap]:
        def _pred(state: StateMap) -> bool:
            value = signal_from(state, bus, can_id, name)
            if value is None:
                return False
            return bool(predicate(value)) if predicate is not None else value == expected

        return self.wait_for(_pred, timeout_s=timeout_s)

    def wait_live(self, bus: str, can_id: int, timeout_s: Optional[float] = None):
        return self.wait_for(lambda s: is_live(s.get((bus, int(can_id)))), timeout_s=timeout_s)

    def wait_rate(self, bus: str, can_id: int, min_hz: float, timeout_s: float = 6.0):
        def _pred(state: StateMap) -> bool:
            m = state.get((bus, int(can_id)))
            if not is_live(m):
                return False
            return float(m.get("observed_rate_hz") or 0.0) >= min_hz

        return self.wait_for(_pred, timeout_s=timeout_s)

    # ── injection ────────────────────────────────────────────────────────
    def inject(
        self,
        bus: str,
        key: str,
        values: Optional[dict] = None,
        period_ms: Optional[float] = None,
        can_id: Optional[int] = None,
        owner: str = "hw_bench",
    ) -> tuple[bool, dict]:
        vals = dict(values or {})
        if key in self._supervised and "rolling_counter" not in vals:
            ctr = self._hb_ctr.get(key, 1)
            vals["rolling_counter"] = ctr
            self._hb_ctr[key] = (ctr + 1) & 0xFF
        code, res = self.request(
            "POST",
            "/injections",
            {
                "bus": bus,
                "key": key,
                "can_id": can_id,
                "values": vals,
                "period_ms": period_ms,
                "owner": owner,
            },
        )
        return code == 200 and bool(res.get("ok")), (res or {})

    def inject_raw(self, bus: str, can_id: int, data_hex: str = "") -> bool:
        code, res = self.request(
            "POST",
            "/injections/raw",
            {
                "bus": bus,
                "can_id": int(can_id),
                "data_hex": data_hex,
                "confirm_raw": True,
                "owner": "hw_bench:raw",
            },
        )
        return code == 200 and bool(res.get("ok"))

    def stop_all(self) -> None:
        try:
            self.cancel_all_injections()
        except Exception:  # noqa: BLE001
            pass

    # ── bench operations ─────────────────────────────────────────────────
    def command_power(self, on: bool) -> tuple[bool, StateMap]:
        req = 1 if on else 0
        for _ in range(3):
            self.inject(HIGH, KEY_PWR_REQ, {"req_start": req})
            time.sleep(0.02)
        return self.wait_signal(LOW, CAN_SYS_PWR_CMD, "power_state", expected=req, timeout_s=3.0)

    def command_mode(self, auto: bool, wait_rt: bool = True) -> tuple[bool, StateMap]:
        req = MODE_AUTO if auto else MODE_MANUAL
        for _ in range(3):
            self.inject(HIGH, KEY_MODE_REQ, {"req_mode": req})
            time.sleep(0.02)
        ok, state = self.wait_signal(LOW, CAN_SYS_MODE_CMD, "mode", expected=req, timeout_s=3.0)
        if ok and wait_rt:
            ok, state = self.wait_signal(HIGH, CAN_RT_STATE_RPT, "mode", expected=req, timeout_s=3.0)
        return ok, state

    def start_drive(
        self,
        speed_mmps: int,
        yaw_rate_mrad_s: int = 0,
        gear: Optional[int] = None,
        period_ms: float = 20.0,
    ) -> Optional[str]:
        vals = {"speed_mmps": int(speed_mmps), "yaw_rate_mrad_s": int(yaw_rate_mrad_s)}
        if gear is not None:
            vals["gear"] = int(gear)
        ok, res = self.inject(HIGH, KEY_HOST_DRIVE, vals, period_ms=period_ms)
        return res.get("job_id") if ok else None

    def send_brake(self, kpa: int) -> bool:
        return self.inject(HIGH, KEY_HOST_BRAKE, {"brake_pressure_kpa": int(kpa)})[0]

    def send_lights(self, left: int = 0, right: int = 0, brake: int = 0, head: int = 0) -> bool:
        return self.inject(
            HIGH,
            KEY_HOST_LIGHT,
            {"left_turn": left, "right_turn": right, "brake_light": brake, "headlight": head},
        )[0]

    def send_obstacle(self, distance_mm: int) -> bool:
        return self.inject(HIGH, KEY_HOST_OBSTACLE, {"distance_mm": int(distance_mm)})[0]

    # ── safety ───────────────────────────────────────────────────────────
    def assert_estop(self, bus: str = HIGH) -> bool:
        return self.inject_raw(bus, CAN_SAFETY_ESTOP, "")

    def reset_estop(self, timeout_s: float = 8.0) -> bool:
        """Clear a latched ESTOP via the staged host reset request.

        The request is inherently multi-frame: the first frame only establishes
        the SYS ``StreamValidity`` baseline; a later counter-advancing frame
        restores authority and is accepted. Send several advancing frames to
        also cover reorder/reacquire, then clear the host latch.
        """
        for seq in range(1, 6):
            self.inject(
                HIGH,
                KEY_ESTOP_RESET_REQ,
                {"request_seq": seq, "reset_token": ESTOP_RESET_TOKEN},
            )
            time.sleep(0.03)
        try:
            self.clear_host_estop()
        except Exception:  # noqa: BLE001
            pass
        ok, _ = self.wait_signal(
            LOW, CAN_SYS_SAFETY_STS, "estop_active", expected=0, timeout_s=timeout_s
        )
        return ok

    def ensure_operational(self) -> None:
        """Clear a latched ESTOP (e.g. a node rebooted into ESTOP) if needed."""
        try:
            if (self.get_status().get("estop") or {}).get("active"):
                self.reset_estop()
        except Exception:  # noqa: BLE001
            pass

    def park(self) -> None:
        """Return the bench to a safe idle before the next test."""
        self.stop_all()
        try:
            self.inject(HIGH, KEY_HOST_DRIVE, {"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0})
            self.send_brake(0)
            self.send_obstacle(OBSTACLE_CLEAR)
            self.send_lights()
        except Exception:  # noqa: BLE001
            pass
        try:
            if (self.get_status().get("estop") or {}).get("active"):
                self.reset_estop()
        except Exception:  # noqa: BLE001
            pass
        try:
            self.command_mode(False, wait_rt=False)
        except Exception:  # noqa: BLE001
            pass
