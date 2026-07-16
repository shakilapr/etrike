"""Source ownership leases."""


def _ready(client):
    ses = client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()[
        "session"
    ]
    sid = ses["session_id"]
    client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    )
    return sid


def test_claim_and_renew_lease(client):
    sid = _ready(client)
    r = client.post(
        f"/api/v1/sessions/{sid}/leases",
        json={"bus": "high", "can_id": 0x300, "owner": "ui", "ttl_s": 5},
    )
    assert r.status_code == 200
    lease_id = r.json()["lease_id"]
    r = client.post(
        f"/api/v1/sessions/{sid}/leases/renew",
        json={"lease_id": lease_id, "ttl_s": 10},
    )
    assert r.status_code == 200
    assert r.json()["renewed"] is True


def test_ownership_conflict_via_lease_api(client):
    sid = _ready(client)
    client.post(
        f"/api/v1/sessions/{sid}/leases",
        json={"bus": "high", "can_id": 0x300, "owner": "a"},
    )
    r = client.post(
        f"/api/v1/sessions/{sid}/leases",
        json={"bus": "high", "can_id": 0x300, "owner": "b"},
    )
    assert r.status_code == 409
    assert r.json()["code"] == "ownership.conflict"


def test_release_lease(client):
    sid = _ready(client)
    lease_id = client.post(
        f"/api/v1/sessions/{sid}/leases",
        json={"bus": "low", "can_id": 0x7FE, "owner": "sys"},
    ).json()["lease_id"]
    r = client.delete(f"/api/v1/sessions/{sid}/leases/{lease_id}")
    assert r.status_code == 200
    # reclaim after release
    r = client.post(
        f"/api/v1/sessions/{sid}/leases",
        json={"bus": "low", "can_id": 0x7FE, "owner": "other"},
    )
    assert r.status_code == 200


def test_stop_all_clears_leases(client):
    sid = _ready(client)
    client.post(
        f"/api/v1/sessions/{sid}/leases",
        json={"bus": "high", "can_id": 0x300, "owner": "a"},
    )
    ses = client.get("/api/v1/sessions").json()["session"]
    client.post(
        f"/api/v1/sessions/{sid}/stop-all",
        json={"expected_revision": ses["revision"]},
    )
    ses = client.get("/api/v1/sessions").json()["session"]
    assert ses["leases"] == []
