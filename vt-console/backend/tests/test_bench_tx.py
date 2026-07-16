"""Bench TX guards: requires session, auto-disables on stop/close (workplan §3.3)."""

from __future__ import annotations


def _session(client):
    return client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()["session"]


def test_enable_requires_session(client):
    r = client.post("/api/v1/sessions/ses_none/bench-tx", json={"enabled": True})
    assert r.status_code == 404  # no active session with that id


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
    r = client.post(f"/api/v1/sessions/{sid}/bench-tx", json={"enabled": False, "expected_revision": rev})
    assert r.json()["session"]["bench_tx"] == "disabled"


def test_stop_all_disables_bench_tx(client):
    ses = _session(client)
    sid = ses["session_id"]
    ses = client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    ).json()["session"]
    r = client.post(f"/api/v1/sessions/{sid}/stop-all", json={"expected_revision": ses["revision"]})
    assert r.json()["session"]["bench_tx"] == "disabled"
    assert r.json()["session"]["phase"] == "running"  # Stop All neutralizes, session stays active


def test_close_disables_bench_tx(client):
    ses = _session(client)
    sid = ses["session_id"]
    ses = client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    ).json()["session"]
    r = client.request("DELETE", f"/api/v1/sessions/{sid}", json={"expected_revision": ses["revision"]})
    assert r.json()["session"]["bench_tx"] == "disabled"


def test_revision_conflict_on_bench_tx(client):
    ses = _session(client)
    r = client.post(
        f"/api/v1/sessions/{ses['session_id']}/bench-tx",
        json={"enabled": True, "expected_revision": 99999},
    )
    assert r.status_code == 409
    assert r.json()["code"] == "session.revision_conflict"
