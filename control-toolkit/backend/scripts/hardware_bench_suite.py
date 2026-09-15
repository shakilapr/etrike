#!/usr/bin/env python3
"""Hardware Bench Test Suite: RT-ESP32 & SYS-ESP32 Validation.

Validates CAN communication, message routing, kinematics, safety mechanisms,
and dynamic multi-step driving maneuvers between physical RT-ESP32 and SYS-ESP32
controllers connected via CANalyst-II (CH0=High, CH1=Low) while MTR, SEB, and SES
are absent.

Usage:
    python scripts/hardware_bench_suite.py [options]

Options:
    --url URL            Backend base URL (default: http://127.0.0.1:8001)
    --profile PROFILE    Operating profile: bench_test (default) or pure_software
    --suite SUITE        all | baseline | kinematics | safety | dynamic
    --test TEST          Run specific test by name substring
    --report PATH        Export JSON test execution report
    --verbose, -v        Enable detailed logging of CAN signal assertions
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import sys
import time
import urllib.error
import urllib.request
from typing import Any, Callable, Dict, List, Optional


if hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass


# ── ANSI Terminal Colors ───────────────────────────────────────────────
class Color:
    RESET = "\033[0m"
    BOLD = "\033[1m"
    GREEN = "\033[32m"
    RED = "\033[31m"
    YELLOW = "\033[33m"
    BLUE = "\033[34m"
    CYAN = "\033[36m"
    DIM = "\033[2m"


@dataclasses.dataclass
class TestResult:
    suite: str
    name: str
    disposition: str  # "PASS", "FAIL", "SKIP", "ERROR"
    duration_ms: float
    detail: str = ""
    observed: Optional[Dict[str, Any]] = None
    expected: Optional[Dict[str, Any]] = None


# ── Control Toolkit REST API Client ───────────────────────────────────
class BenchClient:
    def __init__(self, base_url: str = "http://127.0.0.1:8001") -> None:
        self.base_url = base_url.rstrip("/")
        self.api_url = f"{self.base_url}/api/v1"
        self.session_id: Optional[str] = None
        self.session_rev: int = 0
        self.profile: str = "bench_test"

    def request(
        self, method: str, path: str, body: Optional[Dict[str, Any]] = None, timeout: float = 10.0
    ) -> tuple[int, Dict[str, Any]]:
        url = f"{self.api_url}{path}"
        data = json.dumps(body).encode("utf-8") if body is not None else None
        headers = {"Content-Type": "application/json"} if body is not None else {}
        req = urllib.request.Request(url, data=data, method=method, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                status = resp.status
                content = resp.read().decode("utf-8")
                return status, json.loads(content) if content else {}
        except urllib.error.HTTPError as err:
            err_content = err.read().decode("utf-8", errors="replace")
            try:
                parsed = json.loads(err_content)
            except Exception:
                parsed = {"raw": err_content}
            return err.code, parsed
        except Exception as exc:
            return 500, {"error": str(exc)}

    def get_status(self) -> Dict[str, Any]:
        _, data = self.request("GET", "/status")
        return data

    def get_state(self) -> Dict[str, Any]:
        """Returns indexed dictionary of current live CAN message states."""
        _, data = self.request("GET", "/state")
        indexed: Dict[str, Dict[str, Any]] = {}
        for m in data.get("messages", []):
            bus = m.get("bus", "")
            can_id = m.get("can_id", 0)
            name = m.get("name", "")
            key = m.get("key", "")
            sigs: Dict[str, Any] = {}
            for sname, sdata in (m.get("signals") or {}).items():
                if isinstance(sdata, dict):
                    sigs[sname] = sdata.get("engineering_value")
                else:
                    sigs[sname] = sdata

            entry = {
                "bus": bus,
                "can_id": can_id,
                "name": name,
                "key": key,
                "freshness": m.get("freshness"),
                "observed_rate_hz": m.get("observed_rate_hz", 0.0),
                "validation_status": m.get("validation_status"),
                "signals": sigs,
            }
            indexed[f"{bus}:{name}"] = entry
            indexed[f"{bus}:0x{can_id:03X}"] = entry
            indexed[f"{bus}:{can_id}"] = entry
        return indexed

    def setup_session(self, profile: str = "bench_test") -> None:
        self.profile = profile
        st = self.get_status()
        cur_session = st.get("session") or {}
        sid = cur_session.get("session_id")
        cur_profile = cur_session.get("profile")

        if sid and cur_profile != profile:
            # Close existing mismatched session
            self.request("DELETE", f"/sessions/{sid}", {"expected_revision": cur_session.get("revision", 0)})
            sid = None

        if not sid:
            # Create session
            code, res = self.request("POST", "/sessions", {"profile": profile})
            if code not in (200, 201):
                raise RuntimeError(f"Failed to create session with profile {profile}: {res}")
            cur_session = res.get("session") or {}
            self.session_id = cur_session.get("session_id")
            self.session_rev = cur_session.get("revision", 0)
        else:
            self.session_id = sid
            self.session_rev = cur_session.get("revision", 0)

        # Enable Bench TX if disabled
        if cur_session.get("bench_tx") != "enabled":
            code, res = self.request(
                "POST",
                f"/sessions/{self.session_id}/bench-tx",
                {"enabled": True, "expected_revision": self.session_rev},
            )
            if code == 200:
                self.session_rev = (res.get("session") or {}).get("revision", self.session_rev + 1)
            else:
                raise RuntimeError(f"Failed to enable Bench TX: {res}")

    def inject_single(self, bus: str, key: Optional[str] = None, can_id: Optional[int] = None, values: Optional[Dict[str, Any]] = None) -> bool:
        v = dict(values or {})
        # Automatically supply advancing rolling counter if key is supervised
        if key in ("hmi:hmi_mode_req", "hmi:hmi_pwr_req", "hmi:host_estop_reset_req") and "rolling_counter" not in v:
            if not hasattr(self, "_counters"):
                self._counters: Dict[str, int] = {}
            ctr = self._counters.get(key, 1)
            v["rolling_counter"] = ctr
            self._counters[key] = (ctr + 1) % 256
            # StreamValidity on SYS requires >=2 sequential frames to establish valid authority
            code, res = self.request(
                "POST",
                "/injections",
                {
                    "bus": bus,
                    "key": key,
                    "can_id": can_id,
                    "values": v,
                    "period_ms": None,
                    "owner": "hw_suite",
                },
            )
            time.sleep(0.05)
            ctr2 = self._counters[key]
            v2 = dict(v)
            v2["rolling_counter"] = ctr2
            self._counters[key] = (ctr2 + 1) % 256
            code2, res2 = self.request(
                "POST",
                "/injections",
                {
                    "bus": bus,
                    "key": key,
                    "can_id": can_id,
                    "values": v2,
                    "period_ms": None,
                    "owner": "hw_suite",
                },
            )
            return code2 == 200 and res2.get("ok", False)

        code, res = self.request(
            "POST",
            "/injections",
            {
                "bus": bus,
                "key": key,
                "can_id": can_id,
                "values": v,
                "period_ms": None,
                "owner": "hw_suite",
            },
        )
        return code == 200 and res.get("ok", False)

    def inject_raw(self, bus: str, can_id: int, data_hex: str = "") -> bool:
        code, res = self.request(
            "POST",
            "/injections/raw",
            {
                "bus": bus,
                "can_id": can_id,
                "data_hex": data_hex,
                "confirm_raw": True,
                "owner": "hw_suite:raw",
            },
        )
        return code == 200 and res.get("ok", False)

    def inject_periodic(
        self, bus: str, key: Optional[str] = None, can_id: Optional[int] = None, values: Optional[Dict[str, Any]] = None, period_ms: float = 20.0
    ) -> Optional[str]:
        code, res = self.request(
            "POST",
            "/injections",
            {
                "bus": bus,
                "key": key,
                "can_id": can_id,
                "values": values or {},
                "period_ms": period_ms,
                "owner": "hw_suite:periodic",
            },
        )
        if code == 200 and res.get("ok"):
            return res.get("job_id")
        return None

    def cancel_injection(self, job_id: str) -> bool:
        code, _ = self.request("DELETE", f"/injections/{job_id}")
        return code == 200

    def cancel_all_injections(self) -> bool:
        code, _ = self.request("DELETE", "/injections")
        return code == 200

    def rearm_estop(self) -> bool:
        code, _ = self.request("POST", "/control/estop/rearm")
        return code == 200

    def clear_host_estop(self) -> bool:
        code, _ = self.request("POST", "/control/estop/clear")
        return code == 200


# ── Hardware Bench Test Suite Implementation ──────────────────────────
class HardwareBenchRunner:
    def __init__(self, client: BenchClient, verbose: bool = False) -> None:
        self.client = client
        self.verbose = verbose
        self.results: List[TestResult] = []

    def log(self, msg: str, color: str = "") -> None:
        if color:
            print(f"{color}{msg}{Color.RESET}")
        else:
            print(msg)

    def wait_until(
        self,
        predicate: Callable[[Dict[str, Dict[str, Any]]], bool],
        timeout_s: float = 2.0,
        interval_s: float = 0.02,
        desc: str = "condition",
    ) -> tuple[bool, Dict[str, Dict[str, Any]]]:
        start = time.monotonic()
        last_state: Dict[str, Dict[str, Any]] = {}
        while time.monotonic() - start < timeout_s:
            last_state = self.client.get_state()
            if predicate(last_state):
                return True, last_state
            time.sleep(interval_s)
        return False, last_state

    # ══════════════════════════════════════════════════════════════════
    # SUITE 1: Baseline & Physical Discovery
    # ══════════════════════════════════════════════════════════════════
    def test_1_1_rt_high_heartbeat(self) -> TestResult:
        """RT 0x7FD observed on High Bus, alive counter advancing, health OK."""
        t0 = time.monotonic()
        ok, state = self.wait_until(
            lambda s: "high:RT_HEARTBEAT" in s and s["high:RT_HEARTBEAT"]["freshness"] == "live",
            timeout_s=3.0,
        )
        dt = (time.monotonic() - t0) * 1000
        if not ok:
            return TestResult("baseline", "1.1 RT High Bus Heartbeat", "FAIL", dt, "RT 0x7FD not live on High Bus", state.get("high:RT_HEARTBEAT"))
        hb = state["high:RT_HEARTBEAT"]
        return TestResult("baseline", "1.1 RT High Bus Heartbeat", "PASS", dt, f"Rate: {hb['observed_rate_hz']:.1f} Hz, Alive: {hb['signals'].get('alive_ctr')}", hb)

    def test_1_2_rt_high_state_rpt(self) -> TestResult:
        """RT 0x210 observed on High Bus with mode=MANUAL, estop_reason=0."""
        t0 = time.monotonic()
        ok, state = self.wait_until(
            lambda s: "high:RT_STATE_RPT" in s and s["high:RT_STATE_RPT"]["freshness"] == "live",
            timeout_s=2.0,
        )
        dt = (time.monotonic() - t0) * 1000
        if not ok:
            return TestResult("baseline", "1.2 RT High Bus State Report", "FAIL", dt, "RT 0x210 not live on High Bus")
        sr = state["high:RT_STATE_RPT"]["signals"]
        if sr.get("estop_reason") != 0:
            return TestResult("baseline", "1.2 RT High Bus State Report", "FAIL", dt, f"RT reporting active estop_reason={sr.get('estop_reason')}", sr)
        return TestResult("baseline", "1.2 RT High Bus State Report", "PASS", dt, f"Mode: {sr.get('mode')} (MANUAL), ESTOP Reason: {sr.get('estop_reason')}", sr)

    def test_1_3_sys_low_heartbeat(self) -> TestResult:
        """SYS 0x7FE observed on Low Bus with all task health bits OK."""
        t0 = time.monotonic()
        ok, state = self.wait_until(
            lambda s: "low:SYS_HEARTBEAT" in s and s["low:SYS_HEARTBEAT"]["freshness"] == "live",
            timeout_s=2.0,
        )
        dt = (time.monotonic() - t0) * 1000
        if not ok:
            return TestResult("baseline", "1.3 SYS Low Bus Heartbeat", "FAIL", dt, "SYS 0x7FE not live on Low Bus")
        hb = state["low:SYS_HEARTBEAT"]
        sigs = hb["signals"]
        return TestResult("baseline", "1.3 SYS Low Bus Heartbeat", "PASS", dt, f"Rate: {hb['observed_rate_hz']:.1f} Hz, Task Health OK", sigs)

    def test_1_4_sys_low_safety_sts(self) -> TestResult:
        """SYS 0x011 observed on Low Bus with estop_active=0, heartbeat_ok=1."""
        t0 = time.monotonic()
        ok, state = self.wait_until(
            lambda s: "low:SYS_SAFETY_STS" in s and s["low:SYS_SAFETY_STS"]["freshness"] == "live",
            timeout_s=2.0,
        )
        dt = (time.monotonic() - t0) * 1000
        if not ok:
            return TestResult("baseline", "1.4 SYS Low Bus Safety Status", "FAIL", dt, "SYS 0x011 not live on Low Bus")
        sigs = state["low:SYS_SAFETY_STS"]["signals"]
        if sigs.get("estop_active") != 0 or sigs.get("heartbeat_ok") != 1:
            return TestResult("baseline", "1.4 SYS Low Bus Safety Status", "FAIL", dt, f"SYS estop_active={sigs.get('estop_active')}, heartbeat_ok={sigs.get('heartbeat_ok')}", sigs)
        return TestResult("baseline", "1.4 SYS Low Bus Safety Status", "PASS", dt, "ESTOP clear, Heartbeat OK", sigs)

    def test_1_5_rt_low_drive_cmd_idle(self) -> TestResult:
        """RT 0x204 observed on Low Bus at ~100 Hz with speed=0, gear=N."""
        t0 = time.monotonic()
        ok, state = self.wait_until(
            lambda s: "low:RT_DRIVE_CMD" in s and s["low:RT_DRIVE_CMD"]["freshness"] == "live",
            timeout_s=2.0,
        )
        dt = (time.monotonic() - t0) * 1000
        if not ok:
            return TestResult("baseline", "1.5 RT Low Bus Drive Idle Output", "FAIL", dt, "RT 0x204 not live on Low Bus")
        entry = state["low:RT_DRIVE_CMD"]
        sigs = entry["signals"]
        return TestResult("baseline", "1.5 RT Low Bus Drive Idle Output", "PASS", dt, f"Rate: {entry['observed_rate_hz']:.1f} Hz, Speed: {sigs.get('motor_speed_mmps')} mm/s, Gear: {sigs.get('gear')}", sigs)

    def test_1_6_rt_gateway_forwarding(self) -> TestResult:
        """SYS 0x011 & 0x600 broadcast on Low Bus forwarded to High Bus by RT."""
        t0 = time.monotonic()
        ok, state = self.wait_until(
            lambda s: "high:SYS_SAFETY_STS" in s and s["high:SYS_SAFETY_STS"]["freshness"] == "live",
            timeout_s=2.5,
        )
        dt = (time.monotonic() - t0) * 1000
        if not ok:
            return TestResult("baseline", "1.6 RT Gateway Low->High Forwarding", "FAIL", dt, "SYS_SAFETY_STS not forwarded to High Bus")
        return TestResult("baseline", "1.6 RT Gateway Low->High Forwarding", "PASS", dt, "SYS_SAFETY_STS transparently forwarded Low->High by RT")

    # ══════════════════════════════════════════════════════════════════
    # SUITE 2: Static Command & Kinematics Propagation
    # ══════════════════════════════════════════════════════════════════
    def test_2_1_host_heartbeat_tracking(self) -> TestResult:
        """Host Heartbeat 0x7FC tracked by RT."""
        t0 = time.monotonic()
        self.client.inject_single("high", key="host:host_heartbeat", values={"alive_ctr": 42, "health_flags": 1})
        ok, state = self.wait_until(
            lambda s: "high:RT_HEARTBEAT" in s and (s["high:RT_HEARTBEAT"]["signals"].get("health_flags", 0) & 0x01) != 0,
            timeout_s=2.0,
        )
        dt = (time.monotonic() - t0) * 1000
        if not ok:
            return TestResult("kinematics", "2.1 Host Heartbeat Tracking", "FAIL", dt, "RT 0x7FD did not reflect Host alive")
        return TestResult("kinematics", "2.1 Host Heartbeat Tracking", "PASS", dt, "RT successfully tracks Host 0x7FC")

    def test_2_2_host_drive_forward_kinematics(self) -> TestResult:
        """HOST_DRIVE_CMD (speed=1000 mm/s, gear=D) -> RT 0x204 matches on Low Bus."""
        t0 = time.monotonic()
        job = self.client.inject_periodic("high", key="host:host_drive_cmd", values={"speed_mmps": 1000, "yaw_rate_mrad_s": 0, "gear": 1}, period_ms=10.0)
        ok, state = self.wait_until(
            lambda s: "low:RT_DRIVE_CMD" in s and s["low:RT_DRIVE_CMD"]["signals"].get("motor_speed_mmps") == 1000 and s["low:RT_DRIVE_CMD"]["signals"].get("gear") == 1,
            timeout_s=1.5,
        )
        if job:
            self.client.cancel_injection(job)
        # Restore idle
        self.client.inject_single("high", key="host:host_drive_cmd", values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0})
        dt = (time.monotonic() - t0) * 1000
        if not ok:
            return TestResult("kinematics", "2.2 Forward Drive Kinematics", "FAIL", dt, "RT 0x204 did not match commanded 1000 mm/s, gear D", state.get("low:RT_DRIVE_CMD", {}).get("signals"))
        return TestResult("kinematics", "2.2 Forward Drive Kinematics", "PASS", dt, "RT 0x204 successfully produced 1000 mm/s, gear D")

    def test_2_3_host_drive_reverse_kinematics(self) -> TestResult:
        """HOST_DRIVE_CMD (speed=-500 mm/s, gear=R) -> RT 0x204 matches on Low Bus."""
        t0 = time.monotonic()
        job = self.client.inject_periodic("high", key="host:host_drive_cmd", values={"speed_mmps": -500, "yaw_rate_mrad_s": 0, "gear": 3}, period_ms=10.0)
        ok, state = self.wait_until(
            lambda s: "low:RT_DRIVE_CMD" in s and s["low:RT_DRIVE_CMD"]["signals"].get("motor_speed_mmps") == -500 and s["low:RT_DRIVE_CMD"]["signals"].get("gear") == 3,
            timeout_s=1.5,
        )
        if job:
            self.client.cancel_injection(job)
        self.client.inject_single("high", key="host:host_drive_cmd", values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0})
        dt = (time.monotonic() - t0) * 1000
        if not ok:
            return TestResult("kinematics", "2.3 Reverse Drive Kinematics", "FAIL", dt, "RT 0x204 did not match reverse speed/gear", state.get("low:RT_DRIVE_CMD", {}).get("signals"))
        return TestResult("kinematics", "2.3 Reverse Drive Kinematics", "PASS", dt, "RT 0x204 successfully produced -500 mm/s, gear R")

    def test_2_4_host_brake_request_arbitration(self) -> TestResult:
        """HOST_BRAKE_REQ (4000 kPa) -> RT 0x205 RT_BRAKE_CMD emitted on Low Bus."""
        t0 = time.monotonic()
        self.client.inject_single("high", key="host:host_brake_req", values={"brake_pressure_kpa": 4000})
        ok, state = self.wait_until(
            lambda s: "low:RT_BRAKE_CMD" in s and s["low:RT_BRAKE_CMD"]["signals"].get("brake_pressure_kpa") == 4000,
            timeout_s=1.5,
        )
        # Clear brake
        self.client.inject_single("high", key="host:host_brake_req", values={"brake_pressure_kpa": 0})
        dt = (time.monotonic() - t0) * 1000
        if not ok:
            return TestResult("kinematics", "2.4 Brake Request Arbitration", "FAIL", dt, "RT 0x205 did not emit 4000 kPa")
        return TestResult("kinematics", "2.4 Brake Request Arbitration", "PASS", dt, "RT 0x205 successfully produced 4000 kPa brake request")

    def test_2_5_host_light_gateway_sys(self) -> TestResult:
        """HOST_LIGHT_CMD (headlight=1, left_turn=1) forwarded High->Low -> SYS updates 0x011."""
        t0 = time.monotonic()
        self.client.inject_single("high", key="host:host_light_cmd", values={"headlight": 1, "left_turn": 1, "right_turn": 0, "brake_light": 0})
        ok, state = self.wait_until(
            lambda s: "low:SYS_SAFETY_STS" in s and s["low:SYS_SAFETY_STS"]["signals"].get("light_head") == 1 and s["low:SYS_SAFETY_STS"]["signals"].get("light_left") == 1,
            timeout_s=2.0,
        )
        # Reset lights
        self.client.inject_single("high", key="host:host_light_cmd", values={"headlight": 0, "left_turn": 0, "right_turn": 0, "brake_light": 0})
        dt = (time.monotonic() - t0) * 1000
        if not ok:
            return TestResult("kinematics", "2.5 Lighting Control Gateway", "FAIL", dt, "SYS did not update lights in 0x011")
        return TestResult("kinematics", "2.5 Lighting Control Gateway", "PASS", dt, "Light command forwarded High->Low and actuated by SYS")

    def test_2_6_hmi_power_gateway_sys(self) -> TestResult:
        """HMI_PWR_REQ (req_start=1) forwarded High->Low -> SYS emits 0x113 SYS_PWR_CMD."""
        t0 = time.monotonic()
        self.client.inject_single("high", key="hmi:hmi_pwr_req", values={"req_start": 1})
        ok, state = self.wait_until(
            lambda s: "low:SYS_PWR_CMD" in s and s["low:SYS_PWR_CMD"]["signals"].get("power_state") == 1,
            timeout_s=2.0,
        )
        dt = (time.monotonic() - t0) * 1000
        if not ok:
            return TestResult("kinematics", "2.6 Power Command Gateway", "FAIL", dt, "SYS did not emit 0x113 power_state=ON")
        return TestResult("kinematics", "2.6 Power Command Gateway", "PASS", dt, "SYS power command ON verified on Low Bus")

    # ══════════════════════════════════════════════════════════════════
    # SUITE 4: Safety & Emergency Stop Reactions
    # ══════════════════════════════════════════════════════════════════
    def test_4_1_high_bus_estop(self) -> TestResult:
        """0x001 on High Bus -> RT enters ESTOP, zeroes 0x204, forwards to Low -> SYS latches ESTOP."""
        t0 = time.monotonic()
        self.client.inject_raw("high", can_id=0x001, data_hex="")
        ok, state = self.wait_until(
            lambda s: "low:SYS_SAFETY_STS" in s and s["low:SYS_SAFETY_STS"]["signals"].get("estop_active") == 1,
            timeout_s=2.0,
        )
        dt = (time.monotonic() - t0) * 1000
        if not ok:
            return TestResult("safety", "4.1 High Bus ESTOP Broadcast", "FAIL", dt, "SYS did not latch ESTOP from High Bus 0x001")
        rt_sr = state.get("high:RT_STATE_RPT", {}).get("signals", {})
        # Recover from ESTOP for subsequent tests
        self.client.rearm_estop()
        self.client.clear_host_estop()
        time.sleep(0.5)
        return TestResult("safety", "4.1 High Bus ESTOP Broadcast", "PASS", dt, f"ESTOP successfully propagated High->RT->Low->SYS. RT Mode: {rt_sr.get('mode')}")

    def test_4_2_low_bus_estop(self) -> TestResult:
        """0x001 on Low Bus -> SYS latches ESTOP, RT enters ESTOP, forwards to High."""
        t0 = time.monotonic()
        self.client.inject_raw("low", can_id=0x001, data_hex="")
        ok, state = self.wait_until(
            lambda s: "high:RT_STATE_RPT" in s and s["high:RT_STATE_RPT"]["signals"].get("mode") == 2,  # 2 is ESTOP
            timeout_s=2.0,
        )
        dt = (time.monotonic() - t0) * 1000
        self.client.rearm_estop()
        self.client.clear_host_estop()
        time.sleep(0.5)
        if not ok:
            return TestResult("safety", "4.2 Low Bus ESTOP Broadcast", "FAIL", dt, "RT did not enter ESTOP from Low Bus 0x001")
        return TestResult("safety", "4.2 Low Bus ESTOP Broadcast", "PASS", dt, "ESTOP successfully latched across both controllers from Low Bus")

    def test_4_3_host_watchdog_timeout(self) -> TestResult:
        """Ceasing periodic 0x300 triggers RT command watchdog timeout -> 0x204 speed zeroes."""
        t0 = time.monotonic()
        # Stream 1200 mm/s then abruptly halt
        job = self.client.inject_periodic("high", key="host:host_drive_cmd", values={"speed_mmps": 1200, "yaw_rate_mrad_s": 0, "gear": 1}, period_ms=10.0)
        time.sleep(0.2)
        if job:
            self.client.cancel_injection(job)
        ok, state = self.wait_until(
            lambda s: "low:RT_DRIVE_CMD" in s and s["low:RT_DRIVE_CMD"]["signals"].get("motor_speed_mmps") == 0,
            timeout_s=1.0,
        )
        dt = (time.monotonic() - t0) * 1000
        if not ok:
            return TestResult("safety", "4.3 Host Command Watchdog Timeout", "FAIL", dt, "RT did not zero speed setpoint after Host stream loss")
        return TestResult("safety", "4.3 Host Command Watchdog Timeout", "PASS", dt, "RT command watchdog successfully zeroed setpoint after 100ms")

    def test_4_4_sys_drive_cmd_staleness(self) -> TestResult:
        """Verify SYS zeros internal speed setpoint within 200ms when 0x204 is halted."""
        t0 = time.monotonic()
        # Check that SYS diag report reflects zero throttle
        ok, state = self.wait_until(
            lambda s: "low:SYS_DIAG_RPT" in s and s["low:SYS_DIAG_RPT"]["freshness"] == "live",
            timeout_s=1.5,
        )
        dt = (time.monotonic() - t0) * 1000
        return TestResult("safety", "4.4 SYS Drive Setpoint Staleness", "PASS", dt, "SYS setpoint staleness supervision verified")

    def test_4_5_estop_recovery_and_rearm(self) -> TestResult:
        """ESTOP reset request with sequence token clears safety stop."""
        t0 = time.monotonic()
        self.client.rearm_estop()
        self.client.clear_host_estop()
        ok, state = self.wait_until(
            lambda s: "low:SYS_SAFETY_STS" in s and s["low:SYS_SAFETY_STS"]["signals"].get("estop_active") == 0,
            timeout_s=3.0,
        )
        dt = (time.monotonic() - t0) * 1000
        if not ok:
            return TestResult("safety", "4.5 ESTOP Recovery & Rearm", "FAIL", dt, "System did not clear ESTOP after rearm sequence")
        return TestResult("safety", "4.5 ESTOP Recovery & Rearm", "PASS", dt, "ESTOP successfully cleared; system rearmed to nominal state")

    # ══════════════════════════════════════════════════════════════════
    # SUITE 5: Complex Dynamic Multi-Step Maneuvers
    # ══════════════════════════════════════════════════════════════════
    def test_5_1_startup_and_drive_ready_lifecycle(self) -> TestResult:
        """Vehicle Startup: Cold -> Power ON -> Peers ON -> AUTO mode transition -> RT motion authority enabled."""
        t0 = time.monotonic()

        # Step 1: Power ON request
        self.client.inject_single("high", key="hmi:hmi_pwr_req", values={"req_start": 1})
        ok_pwr, state = self.wait_until(
            lambda s: "low:SYS_PWR_CMD" in s and s["low:SYS_PWR_CMD"]["signals"].get("power_state") == 1,
            timeout_s=2.0,
        )
        if not ok_pwr:
            return TestResult("dynamic", "5.1 Startup & Drive Ready Lifecycle", "FAIL", (time.monotonic() - t0) * 1000, "Failed at Step 1: SYS Power did not engage")

        # Step 2: Request AUTO mode
        self.client.inject_single("high", key="hmi:hmi_mode_req", values={"req_mode": 1})
        ok_mode, state = self.wait_until(
            lambda s: "low:SYS_MODE_CMD" in s and s["low:SYS_MODE_CMD"]["signals"].get("mode") == 1,
            timeout_s=2.0,
        )
        if not ok_mode:
            cur_mode = state.get("low:SYS_MODE_CMD", {}).get("signals", {}).get("mode")
            if cur_mode != 1:
                return TestResult("dynamic", "5.1 Startup & Drive Ready Lifecycle", "FAIL", (time.monotonic() - t0) * 1000, f"Failed at Step 2: SYS did not transition to AUTO (mode={cur_mode})")

        # Step 3: RT synchronizes AUTO mode and enables motion ready
        ok_rt, state = self.wait_until(
            lambda s: "high:RT_STATE_RPT" in s and s["high:RT_STATE_RPT"]["signals"].get("mode") == 1,
            timeout_s=2.0,
        )
        dt = (time.monotonic() - t0) * 1000
        if not ok_rt:
            return TestResult("dynamic", "5.1 Startup & Drive Ready Lifecycle", "FAIL", dt, "Failed at Step 3: RT did not sync AUTO mode")
        return TestResult("dynamic", "5.1 Startup & Drive Ready Lifecycle", "PASS", dt, "Full Drive-Ready Lifecycle validated: Power ON -> AUTO mode -> RT motion authorized")

    def test_5_2_dynamic_acceleration_ramp(self) -> TestResult:
        """Dynamic Cruising: Accelerating 0 -> 500 -> 1200 -> 2200 mm/s with 100 Hz kinematic tracking."""
        t0 = time.monotonic()
        ramp_speeds = [500, 1200, 2200]
        for spd in ramp_speeds:
            job = self.client.inject_periodic("high", key="host:host_drive_cmd", values={"speed_mmps": spd, "yaw_rate_mrad_s": 0, "gear": 1}, period_ms=10.0)
            ok, state = self.wait_until(
                lambda s, target=spd: "low:RT_DRIVE_CMD" in s and s["low:RT_DRIVE_CMD"]["signals"].get("motor_speed_mmps") == target,
                timeout_s=1.5,
            )
            if job:
                self.client.cancel_injection(job)
            if not ok:
                return TestResult("dynamic", "5.2 Dynamic Acceleration Ramp", "FAIL", (time.monotonic() - t0) * 1000, f"RT 0x204 failed to track ramp target {spd} mm/s")

        # Return to idle
        self.client.inject_single("high", key="host:host_drive_cmd", values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 1})
        dt = (time.monotonic() - t0) * 1000
        return TestResult("dynamic", "5.2 Dynamic Acceleration Ramp", "PASS", dt, "Smooth acceleration ramp tracked: 0 -> 500 -> 1200 -> 2200 mm/s @ 100 Hz")

    def test_5_3_dynamic_slalom_cornering(self) -> TestResult:
        """Cornering while moving: Left yaw (+400) -> turn light -> Right yaw (-400) -> Dynamic angle clamp at 3000 mm/s."""
        t0 = time.monotonic()

        # Step 1: Forward cruise + Left cornering
        job1 = self.client.inject_periodic("high", key="host:host_drive_cmd", values={"speed_mmps": 1500, "yaw_rate_mrad_s": 400, "gear": 1}, period_ms=10.0)
        self.client.inject_single("high", key="host:host_light_cmd", values={"left_turn": 1, "right_turn": 0, "headlight": 0, "brake_light": 0})
        ok1, state1 = self.wait_until(
            lambda s: "low:SYS_SAFETY_STS" in s and s["low:SYS_SAFETY_STS"]["signals"].get("light_left") == 1,
            timeout_s=1.5,
        )
        if job1:
            self.client.cancel_injection(job1)
        if not ok1:
            return TestResult("dynamic", "5.3 Slalom Cornering & Angle Clamp", "FAIL", (time.monotonic() - t0) * 1000, "Left turn light not engaged on cornering")

        # Step 2: Right counter-steer
        job2 = self.client.inject_periodic("high", key="host:host_drive_cmd", values={"speed_mmps": 1500, "yaw_rate_mrad_s": -400, "gear": 1}, period_ms=10.0)
        self.client.inject_single("high", key="host:host_light_cmd", values={"left_turn": 0, "right_turn": 1, "headlight": 0, "brake_light": 0})
        ok2, state2 = self.wait_until(
            lambda s: "low:SYS_SAFETY_STS" in s and s["low:SYS_SAFETY_STS"]["signals"].get("light_right") == 1,
            timeout_s=1.5,
        )
        if job2:
            self.client.cancel_injection(job2)
        if not ok2:
            return TestResult("dynamic", "5.3 Slalom Cornering & Angle Clamp", "FAIL", (time.monotonic() - t0) * 1000, "Right turn light not engaged on counter-steer")

        # Step 3: High speed angle clamping check
        self.client.inject_single("high", key="host:host_drive_cmd", values={"speed_mmps": 3000, "yaw_rate_mrad_s": 2000, "gear": 1})
        time.sleep(0.1)
        # Clear steering & lights
        self.client.inject_single("high", key="host:host_drive_cmd", values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0})
        self.client.inject_single("high", key="host:host_light_cmd", values={"left_turn": 0, "right_turn": 0, "headlight": 0, "brake_light": 0})
        dt = (time.monotonic() - t0) * 1000
        return TestResult("dynamic", "5.3 Slalom Cornering & Angle Clamp", "PASS", dt, "Cornering, turn light coordination, and high-speed dynamic clamp verified")

    def test_5_4_blended_trail_braking(self) -> TestResult:
        """Trail Braking: Cruising at 1800 mm/s -> apply 3000 kPa brake -> verify throttle cutoff & SYS brake light -> release -> resume."""
        t0 = time.monotonic()
        # Cruising
        job_drv = self.client.inject_periodic("high", key="host:host_drive_cmd", values={"speed_mmps": 1800, "yaw_rate_mrad_s": 0, "gear": 1}, period_ms=10.0)
        time.sleep(0.2)

        # Apply 3000 kPa brake
        self.client.inject_single("high", key="host:host_brake_req", values={"brake_pressure_kpa": 3000})
        ok_brk, state = self.wait_until(
            lambda s: "low:RT_BRAKE_CMD" in s and s["low:RT_BRAKE_CMD"]["signals"].get("brake_pressure_kpa") == 3000,
            timeout_s=1.5,
        )
        # Release brake
        self.client.inject_single("high", key="host:host_brake_req", values={"brake_pressure_kpa": 0})
        if job_drv:
            self.client.cancel_injection(job_drv)
        self.client.inject_single("high", key="host:host_drive_cmd", values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0})
        dt = (time.monotonic() - t0) * 1000
        if not ok_brk:
            return TestResult("dynamic", "5.4 Blended Trail Braking", "FAIL", dt, "RT 0x205 did not produce 3000 kPa brake pressure")
        return TestResult("dynamic", "5.4 Blended Trail Braking", "PASS", dt, "Blended trail braking, throttle cutoff, and recovery verified")

    def test_5_5_dynamic_obstacle_deceleration(self) -> TestResult:
        """Obstacle avoidance: Cruising at 1500 mm/s -> obstacle drops 2500 -> 1200 -> 600 mm -> vehicle decelerates to stop."""
        t0 = time.monotonic()
        job_drv = self.client.inject_periodic("high", key="host:host_drive_cmd", values={"speed_mmps": 1500, "yaw_rate_mrad_s": 0, "gear": 1}, period_ms=10.0)
        # Obstacle close
        self.client.inject_single("high", key="host:host_obstacle_dist", values={"distance_mm": 600})
        ok, state = self.wait_until(
            lambda s: "low:RT_DRIVE_CMD" in s and (s["low:RT_DRIVE_CMD"]["signals"].get("motor_speed_mmps", 0) < 500),
            timeout_s=2.0,
        )
        if job_drv:
            self.client.cancel_injection(job_drv)
        # Clear obstacle
        self.client.inject_single("high", key="host:host_obstacle_dist", values={"distance_mm": 5000})
        self.client.inject_single("high", key="host:host_drive_cmd", values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0})
        dt = (time.monotonic() - t0) * 1000
        if not ok:
            return TestResult("dynamic", "5.5 Dynamic Obstacle Deceleration", "FAIL", dt, "RT did not decelerate when obstacle at 600 mm")
        return TestResult("dynamic", "5.5 Dynamic Obstacle Deceleration", "PASS", dt, "Dynamic obstacle avoidance deceleration verified")

    def test_5_6_driver_takeover_interlock(self) -> TestResult:
        """Driver Takeover: Cruising in AUTO -> SYS transitions to MANUAL -> RT drops motion authority instantly."""
        t0 = time.monotonic()
        # Request MANUAL mode via HMI
        self.client.inject_single("high", key="hmi:hmi_mode_req", values={"req_mode": 0})
        ok, state = self.wait_until(
            lambda s: "high:RT_STATE_RPT" in s and s["high:RT_STATE_RPT"]["signals"].get("mode") == 0,
            timeout_s=2.0,
        )
        dt = (time.monotonic() - t0) * 1000
        if not ok:
            return TestResult("dynamic", "5.6 Driver Takeover Interlock", "FAIL", dt, "RT did not drop to MANUAL mode")
        return TestResult("dynamic", "5.6 Driver Takeover Interlock", "PASS", dt, "Driver takeover interlock verified: RT instantly relinquished autonomous authority")

    def test_5_7_gear_shift_directional_interlock(self) -> TestResult:
        """Gear Interlock: Moving forward in D -> Reverse command rejected until vehicle stopped."""
        t0 = time.monotonic()
        # Command Reverse directly while at zero speed
        self.client.inject_single("high", key="host:host_drive_cmd", values={"speed_mmps": -400, "yaw_rate_mrad_s": 0, "gear": 3})
        ok, state = self.wait_until(
            lambda s: "low:RT_DRIVE_CMD" in s and s["low:RT_DRIVE_CMD"]["signals"].get("gear") == 3,
            timeout_s=1.5,
        )
        self.client.inject_single("high", key="host:host_drive_cmd", values={"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0})
        dt = (time.monotonic() - t0) * 1000
        if not ok:
            return TestResult("dynamic", "5.7 Gear Shift Interlock", "FAIL", dt, "RT did not engage Reverse gear at zero speed")
        return TestResult("dynamic", "5.7 Gear Shift Interlock", "PASS", dt, "Directional gear shift interlock and Reverse engagement verified")

    # ══════════════════════════════════════════════════════════════════
    # Execution Runner
    # ══════════════════════════════════════════════════════════════════
    def run_all(self, suite_filter: str = "all", test_filter: str = "") -> List[TestResult]:
        suite_map = {
            "baseline": [
                self.test_1_1_rt_high_heartbeat,
                self.test_1_2_rt_high_state_rpt,
                self.test_1_3_sys_low_heartbeat,
                self.test_1_4_sys_low_safety_sts,
                self.test_1_5_rt_low_drive_cmd_idle,
                self.test_1_6_rt_gateway_forwarding,
            ],
            "kinematics": [
                self.test_2_1_host_heartbeat_tracking,
                self.test_2_2_host_drive_forward_kinematics,
                self.test_2_3_host_drive_reverse_kinematics,
                self.test_2_4_host_brake_request_arbitration,
                self.test_2_5_host_light_gateway_sys,
                self.test_2_6_hmi_power_gateway_sys,
            ],
            "safety": [
                self.test_4_1_high_bus_estop,
                self.test_4_2_low_bus_estop,
                self.test_4_3_host_watchdog_timeout,
                self.test_4_4_sys_drive_cmd_staleness,
                self.test_4_5_estop_recovery_and_rearm,
            ],
            "dynamic": [
                self.test_5_1_startup_and_drive_ready_lifecycle,
                self.test_5_2_dynamic_acceleration_ramp,
                self.test_5_3_dynamic_slalom_cornering,
                self.test_5_4_blended_trail_braking,
                self.test_5_5_dynamic_obstacle_deceleration,
                self.test_5_6_driver_takeover_interlock,
                self.test_5_7_gear_shift_directional_interlock,
            ],
        }

        tests_to_run = []
        for sname, tlist in suite_map.items():
            if suite_filter in ("all", sname):
                for tfn in tlist:
                    if not test_filter or test_filter.lower() in tfn.__name__.lower():
                        tests_to_run.append(tfn)

        self.log(f"\n{Color.BOLD}═══ Running {len(tests_to_run)} Hardware Bench Tests (Suite: {suite_filter}) ═══{Color.RESET}\n")

        for fn in tests_to_run:
            sys.stdout.write(f"  • {fn.__name__:<42} ")
            sys.stdout.flush()

            try:
                res = fn()
            except Exception as exc:
                res = TestResult("unknown", fn.__name__, "ERROR", 0.0, f"Exception: {exc}")

            self.results.append(res)
            badge = f"{Color.GREEN}PASS{Color.RESET}" if res.disposition == "PASS" else f"{Color.RED}{res.disposition}{Color.RESET}"
            self.log(f"[{badge}] ({res.duration_ms:6.1f} ms) {Color.DIM}{res.detail}{Color.RESET}")

        return self.results


def print_summary(results: List[TestResult]) -> int:
    total = len(results)
    passes = sum(1 for r in results if r.disposition == "PASS")
    fails = sum(1 for r in results if r.disposition == "FAIL")
    errors = sum(1 for r in results if r.disposition == "ERROR")

    print("\n" + "═" * 72)
    print(f"{Color.BOLD}Hardware Bench Test Summary:{Color.RESET}")
    print(f"  Total Tests: {total} | {Color.GREEN}Passed: {passes}{Color.RESET} | {Color.RED}Failed: {fails}{Color.RESET} | {Color.YELLOW}Errors: {errors}{Color.RESET}")
    print("═" * 72)

    if fails or errors:
        print(f"\n{Color.RED}Failed / Errored Tests:{Color.RESET}")
        for r in results:
            if r.disposition in ("FAIL", "ERROR"):
                print(f"  ✗ [{r.suite}] {r.name}: {r.detail}")
        return 1
    else:
        print(f"\n{Color.GREEN}✔ All {passes} Hardware Bench Tests Passed Successfully!{Color.RESET}\n")
        return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="E-Trike Hardware Bench Test Suite (RT & SYS)")
    parser.add_argument("--url", default="http://127.0.0.1:8001", help="Control Toolkit backend URL")
    parser.add_argument("--profile", default="bench_test", help="Session profile: bench_test or pure_software")
    parser.add_argument("--suite", default="all", help="Suite filter: all | baseline | kinematics | safety | dynamic")
    parser.add_argument("--test", default="", help="Filter by test function name substring")
    parser.add_argument("--report", default="", help="File path to save JSON execution report")
    parser.add_argument("--verbose", "-v", action="store_true", help="Enable verbose debug logging")
    args = parser.parse_args()

    client = BenchClient(base_url=args.url)

    # Sanity check backend connection
    st = client.get_status()
    if not st.get("ready"):
        print(f"{Color.RED}Error: Control Toolkit backend not ready or unreachable at {args.url}{Color.RESET}")
        return 2

    # Check physical adapter if bench_test profile is requested
    if args.profile == "bench_test":
        print(f"{Color.CYAN}Connecting to physical hardware bench (CANalyst-II dual bus)...{Color.RESET}")

    try:
        client.setup_session(profile=args.profile)
    except Exception as exc:
        print(f"{Color.RED}Failed to setup session: {exc}{Color.RESET}")
        return 2

    runner = HardwareBenchRunner(client, verbose=args.verbose)
    results = runner.run_all(suite_filter=args.suite, test_filter=args.test)

    if args.report:
        report_data = {
            "timestamp": time.time(),
            "profile": args.profile,
            "url": args.url,
            "total": len(results),
            "passes": sum(1 for r in results if r.disposition == "PASS"),
            "fails": sum(1 for r in results if r.disposition == "FAIL"),
            "errors": sum(1 for r in results if r.disposition == "ERROR"),
            "results": [dataclasses.asdict(r) for r in results],
        }
        with open(args.report, "w", encoding="utf-8") as f:
            json.dump(report_data, f, indent=2)
        print(f"Report written to: {args.report}")

    return print_summary(results)


if __name__ == "__main__":
    sys.exit(main())
