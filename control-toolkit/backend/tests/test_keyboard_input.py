"""Phase 7 keyboard/control intent pipeline on virtual buses."""

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


def test_intent_requires_bench_tx(client):
    client.post("/api/v1/sessions", json={"profile": "pure_software"})
    r = client.post(
        "/api/v1/control/intent",
        json={"sequence": 1, "throttle": 0.5, "steer": 0.0, "mode": "kinematics"},
    )
    assert r.status_code == 409


def test_kinematics_intent_shapes_host_drive(client):
    _tx(client)
    r = client.post(
        "/api/v1/control/intent",
        json={
            "sequence": 1,
            "source": "keyboard",
            "mode": "kinematics",
            "throttle": 0.5,
            "steer": 0.25,
            "gear": 1,
        },
    )
    assert r.status_code == 200
    ctrl = r.json()["control"]
    assert ctrl["active"] is True
    assert ctrl["shaped_speed_mmps"] == 1500  # 0.5 * 3000
    assert ctrl["shaped_yaw_mrad_s"] == 750  # 0.25 * 3000
    assert ctrl["gear_label"] == "D"
    # Ownership lease name follows source for diagnostics.
    assert client.app.state.lifecycle.control._state.lease_owner == "control:keyboard"


def test_drive_console_source_owns_distinct_lease(client):
    _tx(client)
    r = client.post(
        "/api/v1/control/intent",
        json={
            "sequence": 1,
            "source": "drive_console",
            "mode": "kinematics",
            "throttle": 0.2,
            "steer": 0,
            "gear": 1,
        },
    )
    assert r.status_code == 200
    assert r.json()["control"]["shaped_speed_mmps"] == 600  # 0.2 * 3000
    assert client.app.state.lifecycle.control._state.lease_owner == "control:drive_console"


def test_stale_sequence_rejected(client):
    _tx(client)
    assert (
        client.post(
            "/api/v1/control/intent",
            json={"sequence": 5, "throttle": 0.1, "steer": 0},
        ).status_code
        == 200
    )
    r = client.post(
        "/api/v1/control/intent",
        json={"sequence": 3, "throttle": 0.2, "steer": 0},
    )
    assert r.status_code == 409
    assert r.json()["code"] == "control.stale_sequence"


def test_release_stops_job(client):
    _tx(client)
    client.post(
        "/api/v1/control/intent",
        json={"sequence": 1, "throttle": 0.3, "steer": 0},
    )
    assert client.app.state.lifecycle.control.snapshot()["job_id"]
    r = client.post("/api/v1/control/release", json={"reason": "blur"})
    assert r.status_code == 200
    assert r.json()["control"]["active"] is False
    assert r.json()["control"]["loss_reason"] == "blur"
    assert r.json()["control"]["job_id"] is None


def test_stale_intent_watchdog(client):
    _tx(client)
    client.post(
        "/api/v1/control/intent",
        json={"sequence": 1, "throttle": 0.4, "steer": 0},
    )
    # Force last_mono into the past
    life = client.app.state.lifecycle
    with life.control._lock:
        life.control._state.last_mono = time.monotonic() - 1.0
    life.control.tick_watchdog()
    snap = life.control.snapshot()
    assert snap["active"] is False
    assert snap["loss_reason"] == "stale_intent"


def test_control_estop_dual_bus(client):
    _tx(client)
    r = client.post(
        "/api/v1/control/intent",
        json={"sequence": 1, "throttle": 0, "steer": 0, "estop": True},
    )
    assert r.status_code == 200
    assert r.json()["control"]["active"] is False
    estop = r.json().get("estop") or []
    buses = {e["bus"] for e in estop}
    assert "high" in buses and "low" in buses


def test_reverse_throttle_uses_rev_limit(client):
    _tx(client)
    r = client.post(
        "/api/v1/control/intent",
        json={"sequence": 1, "throttle": -1.0, "steer": 0, "gear": 3},
    )
    assert r.status_code == 200
    # full reverse → -500 mm/s (shared kMaxSpeedRevMmps)
    assert r.json()["control"]["shaped_speed_mmps"] == -500
    assert r.json()["control"]["gear_label"] == "R"


def test_explicit_reverse_with_positive_pedal_never_commands_forward(client):
    _tx(client)
    r = client.post(
        "/api/v1/control/intent",
        json={"sequence": 1, "throttle": 1.0, "steer": 0, "gear": 3},
    )
    assert r.status_code == 200
    control = r.json()["control"]
    assert control["shaped_speed_mmps"] == -500
    assert control["gear_label"] == "R"
