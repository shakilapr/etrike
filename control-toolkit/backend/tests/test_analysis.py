"""Host drive analysis inject (yaw/speed)."""

from __future__ import annotations

import time


def _tx_session(client):
    ses = client.post(
        "/api/v1/sessions", json={"profile": "pure_software"}
    ).json()["session"]
    sid = ses["session_id"]
    client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    )
    return sid


def test_oneshot_yaw_appears_in_state(client):
    _tx_session(client)
    r = client.post(
        "/api/v1/analysis/host-drive",
        json={"speed_mmps": 200, "yaw_rate_mrad_s": 450, "gear": 1},
    )
    assert r.status_code == 200
    assert r.json()["mode"] == "oneshot"
    assert r.json()["values"]["yaw_rate_mrad_s"] == 450

    deadline = time.time() + 2.0
    found = None
    while time.time() < deadline:
        msgs = client.get("/api/v1/state").json()["messages"]
        found = next(
            (m for m in msgs if m.get("name") == "HOST_DRIVE_CMD"),
            None,
        )
        if found and found.get("signals"):
            break
        time.sleep(0.05)
    assert found is not None
    assert found["signals"]["yaw_rate_mrad_s"]["engineering_value"] == 450
    assert found["signals"]["speed_mmps"]["engineering_value"] == 200


def test_periodic_host_drive_and_stop(client):
    _tx_session(client)
    r = client.post(
        "/api/v1/analysis/host-drive",
        json={
            "speed_mmps": 0,
            "yaw_rate_mrad_s": 100,
            "gear": 1,
            "period_ms": 50,
        },
    )
    assert r.status_code == 200
    assert r.json()["mode"] == "periodic"
    time.sleep(0.15)
    stop = client.post("/api/v1/analysis/stop")
    assert stop.status_code == 200
    assert stop.json()["stopped"] >= 1
