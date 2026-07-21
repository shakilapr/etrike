"""Named inject, raw inject, and async verification cancel."""

from __future__ import annotations

import time


def _session(client, profile: str = "pure_software"):
    ses = client.post("/api/v1/sessions", json={"profile": profile}).json()["session"]
    client.post(
        f"/api/v1/sessions/{ses['session_id']}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    )
    return client.get("/api/v1/sessions").json()["session"]


def test_inject_preview_and_one_shot(client):
    _session(client)
    prev = client.post(
        "/api/v1/injections/preview",
        json={
            "bus": "high",
            "key": "host:host_drive_cmd",
            "values": {"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
        },
    )
    assert prev.status_code == 200
    assert prev.json()["ok"] is True
    inj = client.post(
        "/api/v1/injections",
        json={
            "bus": "high",
            "key": "host:host_drive_cmd",
            "values": {"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
        },
    )
    assert inj.status_code == 200
    assert inj.json()["disposition"] in ("submitted", "accepted")


def test_raw_inject_requires_confirm(client):
    _session(client)
    r = client.post(
        "/api/v1/injections/raw",
        json={"bus": "high", "can_id": 0x300, "data_hex": "", "confirm_raw": False},
    )
    assert r.status_code == 400
    assert r.json()["code"] == "injection.raw_confirm_required"


def test_raw_inject_ok(client):
    _session(client)
    r = client.post(
        "/api/v1/injections/raw",
        json={
            "bus": "high",
            "can_id": 0x001,
            "data_hex": "",
            "confirm_raw": True,
        },
    )
    assert r.status_code == 200
    body = r.json()
    assert body["ok"] is True
    assert body["dlc"] == 0
    assert body["can_id"] == 0x001


def test_verification_async_and_cancel(client):
    _session(client)
    r = client.post(
        "/api/v1/tests",
        json={
            "name": "slow wait",
            "async": True,
            "stimulus": {
                "type": "inject",
                "bus": "high",
                "key": "host:host_drive_cmd",
                "values": {"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            },
            "expect": {
                "type": "signal_equals",
                "bus": "high",
                "name": "HOST_DRIVE_CMD",
                "signal": "speed_mmps",
                "equals": 99999,
                "timeout_ms": 3000,
            },
        },
    )
    assert r.status_code == 200
    test = r.json()["test"]
    assert test["disposition"] == "running"
    tid = test["test_id"]
    c = client.post(f"/api/v1/tests/{tid}/cancel")
    assert c.status_code == 200
    # Wait for worker to observe cancel
    for _ in range(50):
        body = client.get(f"/api/v1/tests/{tid}").json()["test"]
        if body["disposition"] != "running":
            break
        time.sleep(0.05)
    body = client.get(f"/api/v1/tests/{tid}").json()["test"]
    assert body["disposition"] == "canceled"


def test_full_vehicle_blocks_direct(client):
    """Full Vehicle rejects low-bus direct bypass even when TX could arm."""
    ses = _session(client, "pure_software")
    r = client.post(
        "/api/v1/control/direct",
        json={
            "channel": "motor",
            "enabled": True,
            "values": {"motor_speed_mmps": 0, "gear": 0},
        },
    )
    assert r.status_code == 200
    # Force profile to full_vehicle via model (profile switch may open USB).
    life = client.app.state.lifecycle
    from control_toolkit.config import Profile

    with life.sessions._lock:
        life.sessions._state.profile = Profile.FULL_VEHICLE
        life.sessions._state.destination = "physical"
        life.sessions._state.bench_tx = __import__(
            "control_toolkit.models.session", fromlist=["BenchTxState"]
        ).BenchTxState.ENABLED
    r = client.post(
        "/api/v1/control/direct",
        json={
            "channel": "motor",
            "enabled": True,
            "values": {"motor_speed_mmps": 100, "gear": 1},
        },
    )
    assert r.status_code == 409
    assert r.json()["code"] == "control.profile_blocked"
