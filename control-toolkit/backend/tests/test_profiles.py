"""Profile availability and controlled transitions."""


def test_list_profiles(client):
    r = client.get("/api/v1/sessions/profiles")
    assert r.status_code == 200
    by_id = {p["id"]: p for p in r.json()["profiles"]}
    assert by_id["pure_software"]["available"] is True
    assert by_id["bench_test"]["available"] is False
    assert by_id["full_vehicle"]["available"] is False


def test_create_pure_software_session(client):
    r = client.post("/api/v1/sessions", json={"profile": "pure_software"})
    assert r.status_code == 200
    ses = r.json()["session"]
    assert ses["profile"] == "pure_software"
    assert ses["phase"] == "running"
    assert ses["destination"] == "virtual"
    assert ses["bench_tx"] == "disabled"
    assert ses["test_session_id"]
    assert ses["wire_hash"]
    assert ses["semantic_hash"]


def test_full_vehicle_refused_no_silent_virtual(client):
    r = client.post("/api/v1/sessions", json={"profile": "full_vehicle"})
    assert r.status_code == 503
    assert r.json()["code"] == "profile.physical_unavailable"


def test_bench_test_refused(client):
    r = client.post("/api/v1/sessions", json={"profile": "bench_test"})
    assert r.status_code == 503
    assert r.json()["code"] == "profile.physical_unavailable"


def test_profile_change_requires_confirm(client):
    ses = client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()[
        "session"
    ]
    sid = ses["session_id"]
    r = client.post(
        f"/api/v1/sessions/{sid}/profile",
        json={"profile": "pure_software", "confirm": False},
    )
    # same profile with confirm false still requires confirm flag
    assert r.status_code == 400
    assert r.json()["code"] == "profile.confirm_required"


def test_profile_change_to_physical_refused(client):
    ses = client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()[
        "session"
    ]
    sid = ses["session_id"]
    r = client.post(
        f"/api/v1/sessions/{sid}/profile",
        json={
            "profile": "full_vehicle",
            "confirm": True,
            "expected_revision": ses["revision"],
        },
    )
    assert r.status_code == 503
