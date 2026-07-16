"""Session API surface."""


def test_get_sessions_empty(client):
    r = client.get("/api/v1/sessions")
    assert r.status_code == 200
    assert r.json()["session"]["session_id"] is None


def test_status_includes_session_fields(client):
    client.post("/api/v1/sessions", json={"profile": "pure_software"})
    st = client.get("/api/v1/status").json()
    assert st["session"]["phase"] == "running"
    assert st["session"]["destination"] == "virtual"


def test_vehicle_view_updates_session_header_fields(client):
    ses = client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()[
        "session"
    ]
    sid = ses["session_id"]
    r = client.post(
        f"/api/v1/sessions/{sid}/vehicle-view",
        json={
            "requested_mode": "AUTO",
            "confirmed_mode": "MANUAL",
            "requested_power": "ON",
            "confirmed_power": "OFF",
            "estop_active": False,
            "recording": False,
        },
    )
    assert r.status_code == 200
    body = r.json()["session"]
    assert body["requested_mode"] == "AUTO"
    assert body["confirmed_mode"] == "MANUAL"
    assert body["requested_power"] == "ON"
    assert body["confirmed_power"] == "OFF"
    assert body["estop_active"] is False
