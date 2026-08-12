"""Kinematics mode ownership and HOST_DRIVE_CMD generation."""

from __future__ import annotations

import time


def _tx(client):
    ses = client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()[
        "session"
    ]
    client.post(
        f"/api/v1/sessions/{ses['session_id']}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    )


def test_kinematics_owns_drive_and_direct_steering(client):
    _tx(client)
    client.post(
        "/api/v1/control/intent",
        json={"sequence": 1, "throttle": 0.2, "steer": 0.0, "mode": "kinematics"},
    )
    # Give the background scheduler loop a moment to tick and claim ownership of 0x300.
    time.sleep(0.05)
    # Competing inject on same CAN ID with different owner fails
    r = client.post(
        "/api/v1/injections",
        json={
            "bus": "high",
            "key": "host:host_drive_cmd",
            "values": {"speed_mmps": 1, "yaw_rate_mrad_s": 0, "gear": 0},
            "owner": "other",
        },
    )
    assert r.status_code == 409
    status = client.get("/api/v1/control/status").json()["control"]
    assert status["job_id"]
    assert status["steer_job_id"]
    assert status["paths"]["high_kinematics"]["steering_can_id"] == 0x303


def test_stop_all_releases_control(client):
    ses = client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()[
        "session"
    ]
    sid = ses["session_id"]
    ses = client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    ).json()["session"]
    client.post(
        "/api/v1/control/intent",
        json={"sequence": 1, "throttle": 0.5, "steer": 0},
    )
    assert client.get("/api/v1/control/status").json()["control"]["active"]
    ses = client.get("/api/v1/sessions").json()["session"]
    client.post(
        f"/api/v1/sessions/{sid}/stop-all",
        json={"expected_revision": ses["revision"]},
    )
    ctrl = client.get("/api/v1/control/status").json()["control"]
    assert ctrl["active"] is False
    assert ctrl["loss_reason"] == "stop_all"
