"""Direct low-bus actuator streams (motor / steering / brake)."""

from __future__ import annotations

import time


def _tx(client):
    cur = client.get("/api/v1/sessions").json()["session"]
    if cur.get("session_id"):
        client.request(
            "DELETE",
            f"/api/v1/sessions/{cur['session_id']}",
            json={"expected_revision": cur["revision"]},
        )
    ses = client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()[
        "session"
    ]
    client.post(
        f"/api/v1/sessions/{ses['session_id']}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    )


def test_direct_motor_appears_on_low_bus(client):
    _tx(client)
    r = client.post(
        "/api/v1/control/direct",
        json={
            "channel": "motor",
            "enabled": True,
            "values": {"motor_speed_mmps": 400, "gear": 1},
        },
    )
    assert r.status_code == 200
    assert r.json()["control"]["mode"] == "direct"
    assert "motor" in r.json()["control"]["direct_channels"]

    deadline = time.time() + 2.0
    found = None
    while time.time() < deadline:
        msgs = client.get("/api/v1/state").json()["messages"]
        found = next(
            (m for m in msgs if m.get("name") == "RT_DRIVE_CMD" or m.get("can_id") == 0x204),
            None,
        )
        if found and found.get("signals"):
            break
        time.sleep(0.03)
    assert found is not None
    assert found["bus"] == "low"
    assert int(found["signals"]["motor_speed_mmps"]["engineering_value"]) == 400


def test_direct_steering_and_brake_encode(client):
    _tx(client)
    steer = client.post(
        "/api/v1/control/direct",
        json={
            "channel": "steering",
            "enabled": True,
            "values": {
                "target_angle_raw": 50,
                "control_enable": True,
                "alignment_enable": True,
            },
        },
    )
    assert steer.status_code == 200
    brake = client.post(
        "/api/v1/control/direct",
        json={
            "channel": "brake",
            "enabled": True,
            "values": {"pressure_request_raw": 40, "control_enable": True},
        },
    )
    assert brake.status_code == 200
    ch = set(brake.json()["control"]["direct_channels"])
    assert {"steering", "brake"} <= ch

    deadline = time.time() + 2.0
    names = set()
    while time.time() < deadline:
        msgs = client.get("/api/v1/state").json()["messages"]
        names = {m.get("name") for m in msgs}
        if "VCU_SES_REQ" in names and "VCU_SEB_REQ" in names:
            break
        time.sleep(0.03)
    assert "VCU_SES_REQ" in names
    assert "VCU_SEB_REQ" in names


def test_kinematics_and_direct_mutual_exclusion(client):
    _tx(client)
    client.post(
        "/api/v1/control/intent",
        json={"sequence": 1, "throttle": 0.4, "steer": 0, "mode": "kinematics"},
    )
    assert client.get("/api/v1/control/status").json()["control"]["job_id"]

    client.post(
        "/api/v1/control/direct",
        json={
            "channel": "motor",
            "enabled": True,
            "values": {"motor_speed_mmps": 100, "gear": 1},
        },
    )
    st = client.get("/api/v1/control/status").json()["control"]
    assert st["mode"] == "direct"
    assert st["job_id"] is None  # kinematics cancelled
    assert "motor" in st["direct_channels"]

    # Kinematics again clears direct
    client.post(
        "/api/v1/control/intent",
        json={"sequence": 2, "throttle": 0.1, "steer": 0, "mode": "kinematics"},
    )
    st = client.get("/api/v1/control/status").json()["control"]
    assert st["mode"] == "kinematics"
    assert st["direct_channels"] == []
    assert st["job_id"]


def test_direct_stop_channel(client):
    _tx(client)
    client.post(
        "/api/v1/control/direct",
        json={
            "channel": "motor",
            "enabled": True,
            "values": {"motor_speed_mmps": 50, "gear": 1},
        },
    )
    r = client.post(
        "/api/v1/control/direct",
        json={"channel": "motor", "enabled": False},
    )
    assert r.status_code == 200
    assert "motor" not in r.json()["control"]["direct_channels"]
