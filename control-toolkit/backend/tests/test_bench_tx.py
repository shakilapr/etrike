"""Bench TX enable/disable and auto-disable on stop/close."""


def _session(client):
    return client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()[
        "session"
    ]


def test_enable_disable_bench_tx(client):
    ses = _session(client)
    sid = ses["session_id"]
    r = client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    )
    assert r.status_code == 200
    assert r.json()["session"]["bench_tx"] == "enabled"
    rev = r.json()["session"]["revision"]
    r = client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": False, "expected_revision": rev},
    )
    assert r.json()["session"]["bench_tx"] == "disabled"


def test_stop_all_disables_bench_tx(client):
    ses = _session(client)
    sid = ses["session_id"]
    ses = client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    ).json()["session"]
    r = client.post(
        f"/api/v1/sessions/{sid}/stop-all",
        json={"expected_revision": ses["revision"]},
    )
    assert r.json()["session"]["bench_tx"] == "disabled"
    assert r.json()["session"]["phase"] == "running"


def test_close_disables_bench_tx(client):
    ses = _session(client)
    sid = ses["session_id"]
    ses = client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    ).json()["session"]
    r = client.request(
        "DELETE",
        f"/api/v1/sessions/{sid}",
        json={"expected_revision": ses["revision"]},
    )
    assert r.json()["session"]["bench_tx"] == "disabled"


def test_revision_conflict_on_bench_tx(client):
    ses = _session(client)
    r = client.post(
        f"/api/v1/sessions/{ses['session_id']}/bench-tx",
        json={"enabled": True, "expected_revision": 99999},
    )
    assert r.status_code == 409
    assert r.json()["code"] == "session.revision_conflict"
