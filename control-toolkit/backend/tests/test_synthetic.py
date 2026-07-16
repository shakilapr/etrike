"""Analysis stimuli (not full synthetic vehicle)."""

from __future__ import annotations


def test_list_analysis_stimuli(client):
    listed = client.get("/api/v1/synthetic-peers").json()
    names = {p["name"] for p in listed["available"]}
    assert "host_drive_analysis" in names
    # Full peer mesh removed
    assert "sys_heartbeat" not in names
    assert "host_heartbeat" not in names


def test_start_requires_explicit_names(client):
    ses = client.post(
        "/api/v1/sessions", json={"profile": "pure_software"}
    ).json()["session"]
    sid = ses["session_id"]
    client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    )
    r = client.post("/api/v1/synthetic-peers/start", json={})
    assert r.status_code == 400
    assert r.json()["code"] == "synthetic.names_required"


def test_start_requires_bench_tx(client):
    client.post("/api/v1/sessions", json={"profile": "pure_software"})
    r = client.post(
        "/api/v1/synthetic-peers/start",
        json={"names": ["host_drive_analysis"]},
    )
    assert r.status_code == 409
