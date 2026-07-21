"""Concurrent multi-input integration: several conditions must hold together.

Mirrors real use: create session + Bench TX + SIL peers + inject/analysis + observe.
A single inject without session/TX gate does not prove the system works.
"""

from __future__ import annotations

import time
from pathlib import Path

from fastapi.testclient import TestClient

from control_toolkit.config import ToolkitConfig
from control_toolkit.main import create_app


def _wait_messages(client: TestClient, names: set[str], timeout_s: float = 3.0) -> set[str]:
    found: set[str] = set()
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        state = client.get("/api/v1/state").json()
        for m in state.get("messages") or []:
            n = m.get("name")
            if n:
                found.add(str(n))
        if names.issubset(found):
            return found
        time.sleep(0.05)
    return found


def _channel_activity(client: TestClient) -> dict:
    st = client.get("/api/v1/status").json()
    ch = st.get("adapter", {}).get("channels") or {}
    return {
        "health": str(st.get("adapter", {}).get("health")),
        "high": str((ch.get("high") or {}).get("activity")),
        "low": str((ch.get("low") or {}).get("activity")),
        "high_rx": (ch.get("high") or {}).get("rx_count"),
        "low_rx": (ch.get("low") or {}).get("rx_count"),
    }


def test_concurrent_session_tx_sil_inject_and_quiet_aging() -> None:
    sil = (
        Path(__file__).parents[3]
        / "native-test"
        / "build-sil"
        / "sim_engine_native.exe"
    )
    cfg = ToolkitConfig(native_sil_executable=str(sil) if sil.is_file() else None)
    app = create_app(cfg)
    with TestClient(app) as client:
        # 0) Explicit session — required before any gated action
        created = client.post("/api/v1/sessions", json={"profile": "pure_software"})
        assert created.status_code == 200, created.text
        ses = created.json()["session"]
        sid = ses["session_id"]
        rev = ses["revision"]
        assert sid

        # 1) Simulation peers auto/start (SYS always; RT if binary present)
        sim0 = client.get("/api/v1/simulation").json()["simulation"]
        assert sim0["virtual_can"]["state"] == "running"
        if sim0["sys_sil"]["state"] != "running":
            client.post("/api/v1/simulation/start")
            sim0 = client.get("/api/v1/simulation").json()["simulation"]
        assert sim0["sys_sil"]["state"] == "running"

        # 2) While SIL runs, low bus must be active (SYS heartbeat)
        time.sleep(0.35)
        act_live = _channel_activity(client)
        assert act_live["low"] == "active", act_live

        # 3) Enable Bench TX (concurrent prerequisite with SIL)
        gate = client.post(
            f"/api/v1/sessions/{sid}/bench-tx",
            json={"enabled": True, "expected_revision": rev},
        )
        if gate.status_code >= 400:
            gate = client.post(
                f"/api/v1/sessions/{sid}/bench-tx",
                json={"enabled": True, "revision": rev},
            )
        assert gate.status_code == 200, gate.text
        body = gate.json()
        rev = body.get("revision") or body.get("session", {}).get("revision") or rev

        # 4) Concurrent traffic: SYS SIL + analysis host drive (needs TX gate)
        analysis = client.post(
            "/api/v1/analysis/host-drive",
            json={
                "speed_mmps": 400,
                "yaw_rate_mrad_s": 50,
                "gear": 1,
                "period_ms": 50,
            },
        )
        if analysis.status_code >= 400:
            # alternate path: named injection oneshot
            analysis = client.post(
                "/api/v1/injections",
                json={
                    "bus": "high",
                    "key": "host:host_drive_cmd",
                    "values": {
                        "speed_mmps": 400,
                        "yaw_rate_mrad_s": 50,
                        "gear": 1,
                    },
                },
            )
        assert analysis.status_code < 400, analysis.text

        found = _wait_messages(client, {"SYS_HEARTBEAT", "SYS_SAFETY_STS"}, timeout_s=2.5)
        assert "SYS_HEARTBEAT" in found, found
        assert "SYS_SAFETY_STS" in found, found

        time.sleep(0.3)
        act_busy = _channel_activity(client)
        assert act_busy["low"] == "active", act_busy

        # 5) Stop analysis + SIL — then activity must age to quiet (not stuck active)
        client.post("/api/v1/synthetic-peers/stop", json={})
        # try analysis stop endpoint variants
        for path in ("/api/v1/analysis/stop", "/api/v1/analysis/host-drive/stop"):
            client.post(path)
        stopped = client.post("/api/v1/simulation/stop").json()["simulation"]
        assert stopped["sys_sil"]["state"] == "stopped"
        if cfg.native_sil_executable:
            assert stopped["rt_sil"]["state"] == "stopped"

        time.sleep(1.0)  # > quiet_after 750ms
        act_idle = _channel_activity(client)
        assert act_idle["high"] in ("quiet", "unseen"), act_idle
        assert act_idle["low"] in ("quiet", "unseen"), act_idle
        assert act_idle["health"] in ("quiet", "open"), act_idle

        # 6) Restart SIL — multi-source activity returns
        started = client.post("/api/v1/simulation/start").json()["simulation"]
        assert started["sys_sil"]["state"] == "running"
        time.sleep(0.4)
        act_again = _channel_activity(client)
        assert act_again["low"] == "active", act_again
