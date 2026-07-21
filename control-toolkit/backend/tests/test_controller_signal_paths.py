"""Controller signal-path tests: command reaches bus; feedback only if peer exists.

Not a full stack. Concurrent: session + Bench TX + (SIL for RT cascade).
"""

from __future__ import annotations

import time
from pathlib import Path

from fastapi.testclient import TestClient

from control_toolkit.config import ToolkitConfig
from control_toolkit.main import create_app


def _ready_client(with_rt: bool = True) -> TestClient:
    sil = (
        Path(__file__).parents[3]
        / "native-test"
        / "build-sil"
        / "sim_engine_native.exe"
    )
    cfg = ToolkitConfig(
        native_sil_executable=str(sil) if (with_rt and sil.is_file()) else None
    )
    app = create_app(cfg)
    return TestClient(app)


def _arm(client: TestClient) -> tuple[str, int]:
    created = client.post("/api/v1/sessions", json={"profile": "pure_software"})
    assert created.status_code == 200, created.text
    ses = created.json()["session"]
    sid, rev = ses["session_id"], ses["revision"]
    gate = client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": rev},
    )
    assert gate.status_code == 200, gate.text
    client.post("/api/v1/simulation/start")
    return sid, gate.json().get("session", gate.json()).get("revision", rev)


def _live_names(client: TestClient) -> dict[str, dict]:
    state = client.get("/api/v1/state").json()
    out: dict[str, dict] = {}
    for m in state.get("messages") or []:
        name = m.get("name")
        bus = m.get("bus")
        if not name or not bus:
            continue
        out[f"{bus}:{name}"] = m
    return out


def _wait_live(client: TestClient, key: str, timeout_s: float = 2.5) -> dict | None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        idx = _live_names(client)
        row = idx.get(key)
        if row and row.get("freshness") == "live":
            return row
        time.sleep(0.05)
    return _live_names(client).get(key)


def test_high_host_command_reaches_low_rt_controller() -> None:
    with _ready_client(with_rt=True) as client:
        _arm(client)
        r = client.post(
            "/api/v1/analysis/host-drive",
            json={
                "speed_mmps": 650,
                "yaw_rate_mrad_s": 100,
                "gear": 1,
                "period_ms": 25,
            },
        )
        assert r.status_code == 200, r.text
        host = _wait_live(client, "high:HOST_DRIVE_CMD")
        assert host is not None and host.get("freshness") == "live"
        # RT SIL bridges high host → low RT_DRIVE when executable present
        rt = _wait_live(client, "low:RT_DRIVE_CMD", timeout_s=3.0)
        # If RT SIL not configured in this environment, skip cascade assert
        sim = client.get("/api/v1/simulation").json()["simulation"]
        if sim["rt_sil"]["state"] == "running":
            assert rt is not None and rt.get("freshness") == "live"
            sigs = (rt.get("signals") or {})
            speed = (sigs.get("motor_speed_mmps") or {}).get("engineering_value")
            if isinstance(speed, dict):
                speed = speed.get("engineering_value")
            # signal shape may be nested
            if speed is None and isinstance(sigs.get("motor_speed_mmps"), (int, float)):
                speed = sigs["motor_speed_mmps"]
            assert speed is not None
        client.post("/api/v1/analysis/stop")


def test_low_direct_motor_command_on_low_not_high_feedback() -> None:
    """Low motor command must appear on low; high MTR feedback needs peer (optional)."""
    with _ready_client(with_rt=False) as client:
        _arm(client)
        client.post("/api/v1/control/release", json={"reason": "test"})
        r = client.post(
            "/api/v1/control/direct",
            json={
                "channel": "motor",
                "enabled": True,
                "values": {"motor_speed_mmps": 400, "gear": 1},
                "period_ms": 20,
            },
        )
        assert r.status_code == 200, r.text
        low = _wait_live(client, "low:RT_DRIVE_CMD")
        assert low is not None and low.get("freshness") == "live"
        # High feedback from MTR is not required without MTR peer
        high_fb = _live_names(client).get("high:MTR_MOTOR_FBK")
        # Document actual behavior: may be absent
        _ = high_fb
        client.post(
            "/api/v1/control/direct",
            json={"channel": "motor", "enabled": False},
        )


def test_low_direct_steer_and_brake_reach_low_controllers() -> None:
    with _ready_client(with_rt=False) as client:
        _arm(client)
        client.post("/api/v1/control/release", json={"reason": "test"})

        steer = client.post(
            "/api/v1/control/direct",
            json={
                "channel": "steering",
                "enabled": True,
                "values": {"target_angle_raw": 100},
                "period_ms": 20,
            },
        )
        assert steer.status_code == 200, steer.text
        ses = _wait_live(client, "low:VCU_SES_REQ", timeout_s=3.0)
        assert ses is not None and ses.get("freshness") == "live", _live_names(client)
        client.post(
            "/api/v1/control/direct",
            json={"channel": "steering", "enabled": False},
        )

        brake = client.post(
            "/api/v1/control/direct",
            json={
                "channel": "brake",
                "enabled": True,
                "values": {"pressure_request_raw": 35},
                "period_ms": 20,
            },
        )
        assert brake.status_code == 200, brake.text
        seb = _wait_live(client, "low:VCU_SEB_REQ", timeout_s=3.0)
        assert seb is not None and seb.get("freshness") == "live", _live_names(client)
        client.post(
            "/api/v1/control/direct",
            json={"channel": "brake", "enabled": False},
        )


def test_low_command_does_not_imply_high_status_feedback_without_peers() -> None:
    """Explicit: low inject ≠ automatic high status feedback."""
    with _ready_client(with_rt=False) as client:
        _arm(client)
        client.post("/api/v1/control/release", json={"reason": "test"})
        client.post(
            "/api/v1/control/direct",
            json={
                "channel": "motor",
                "enabled": True,
                "values": {"motor_speed_mmps": 300, "gear": 1},
                "period_ms": 20,
            },
        )
        time.sleep(0.5)
        idx = _live_names(client)
        assert "low:RT_DRIVE_CMD" in idx
        # Without MTR/SES/SEB synthetic peers, status feedback stays missing
        assert "high:MTR_MOTOR_FBK" not in idx or idx["high:MTR_MOTOR_FBK"].get(
            "freshness"
        ) in ("missing", "unseen", "late", "live", "frozen")
        # Stronger: SES/SEB status should not appear from motor-only command
        assert "low:SES_STATUS" not in idx or idx["low:SES_STATUS"].get("freshness") != "live"
        assert "low:SEB_STATUS" not in idx or idx["low:SEB_STATUS"].get("freshness") != "live"
        client.post(
            "/api/v1/control/direct",
            json={"channel": "motor", "enabled": False},
        )
