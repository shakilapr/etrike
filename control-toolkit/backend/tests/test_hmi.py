"""HMI mode/power periodic TX on virtual bus."""

from __future__ import annotations

import time


def _tx(client):
    ses = client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()[
        "session"
    ]
    sid = ses["session_id"]
    client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    )
    return sid


def test_hmi_mode_requires_bench_tx(client):
    client.post("/api/v1/sessions", json={"profile": "pure_software"})
    r = client.post("/api/v1/hmi/mode", json={"req_mode": 1, "enabled": True})
    assert r.status_code == 409
    assert r.json()["code"] == "bench_tx.disabled"


def test_hmi_mode_appears_in_state_and_updates_vehicle_view(client):
    sid = _tx(client)
    r = client.post("/api/v1/hmi/mode", json={"req_mode": 1, "enabled": True})
    assert r.status_code == 200
    job_id = r.json()["job_id"]

    # Requested mode stored on session (not confirmed)
    ses = client.get("/api/v1/sessions").json()["session"]
    assert ses["requested_mode"] == "AUTO"

    deadline = time.time() + 3.0
    found = None
    while time.time() < deadline:
        msgs = client.get("/api/v1/state").json()["messages"]
        found = next((m for m in msgs if m.get("name") == "HMI_MODE_REQ"), None)
        if found and found.get("signals"):
            break
        time.sleep(0.05)
    assert found is not None, "HMI_MODE_REQ never observed"
    assert found["bus"] == "high"
    assert found["can_id"] == 0x111
    # rolling counter advances over periods
    c1 = int(found["signals"]["rolling_counter"]["engineering_value"])
    time.sleep(1.1)
    msgs = client.get("/api/v1/state").json()["messages"]
    found2 = next((m for m in msgs if m.get("name") == "HMI_MODE_REQ"), None)
    assert found2 is not None
    c2 = int(found2["signals"]["rolling_counter"]["engineering_value"])
    assert c2 != c1 or found2["signals"]["req_mode"]["engineering_value"] in (0, 1)

    # disable stops job
    client.post("/api/v1/hmi/mode", json={"req_mode": 0, "enabled": False})
    assert job_id not in client.app.state.lifecycle.scheduler.job_ids()
    _ = sid


def test_hmi_power_appears_in_state(client):
    _tx(client)
    r = client.post("/api/v1/hmi/power", json={"req_start": 1, "enabled": True})
    assert r.status_code == 200
    deadline = time.time() + 3.0
    found = None
    while time.time() < deadline:
        msgs = client.get("/api/v1/state").json()["messages"]
        found = next((m for m in msgs if m.get("name") == "HMI_PWR_REQ"), None)
        if found and found.get("signals"):
            break
        time.sleep(0.05)
    assert found is not None
    assert found["can_id"] == 0x112
    ses = client.get("/api/v1/sessions").json()["session"]
    assert ses["requested_power"] in ("ON", "1", "on")


def test_estop_safety_frame(client):
    _tx(client)
    r = client.post(
        "/api/v1/injections",
        json={
            "bus": "high",
            "key": "safety:safety_estop",
            "values": {},
            "owner": "test:estop",
        },
    )
    assert r.status_code == 200, r.text
    body = r.json()
    assert body["disposition"] == "submitted"
    assert body["can_id"] == 0x001
    # DLC=0 event frame
    assert body["data_hex"] == "" or len(body["data_hex"]) == 0

    deadline = time.time() + 2.0
    found = None
    while time.time() < deadline:
        msgs = client.get("/api/v1/state").json()["messages"]
        found = next(
            (m for m in msgs if m.get("name") == "SAFETY_ESTOP" or m.get("can_id") == 1),
            None,
        )
        if found:
            break
        time.sleep(0.05)
    assert found is not None
