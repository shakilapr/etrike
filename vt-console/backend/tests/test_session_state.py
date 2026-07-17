"""Session FSM identity, revision, and Stop-All-on-close (workplan §3.2)."""

from __future__ import annotations


def test_session_fsm_reaches_running(client):
    ses = client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()["session"]
    assert ses["phase"] == "running"
    assert ses["session_id"].startswith("ses_")
    assert ses["test_session_id"].startswith("test_")
    assert ses["revision"] >= 1
    assert ses["adapter_epoch"] is not None


def test_cannot_create_second_active_session(client):
    client.post("/api/v1/sessions", json={"profile": "pure_software"})
    r = client.post("/api/v1/sessions", json={"profile": "pure_software"})
    assert r.status_code == 409
    assert r.json()["code"] == "session.active"


def test_close_with_outcome(client):
    ses = client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()["session"]
    r = client.request(
        "DELETE",
        f"/api/v1/sessions/{ses['session_id']}",
        json={"expected_revision": ses["revision"], "outcome": "completed"},
    )
    assert r.status_code == 200
    body = r.json()["session"]
    assert body["session_id"] is None
    assert body["phase"] == "completed"
    assert body["bench_tx"] == "disabled"


def test_custom_test_session_id(client):
    ses = client.post(
        "/api/v1/sessions", json={"profile": "pure_software", "test_session_id": "run_abc"}
    ).json()["session"]
    assert ses["test_session_id"] == "run_abc"


def test_close_unknown_session_id_is_404(client):
    client.post("/api/v1/sessions", json={"profile": "pure_software"})
    r = client.request("DELETE", "/api/v1/sessions/ses_doesnotexist")
    assert r.status_code == 404
    assert r.json()["code"] == "session.not_found"


def test_close_with_no_active_session_is_idle_snapshot(client):
    r = client.get("/api/v1/sessions")
    assert r.json()["session"]["session_id"] is None
    assert r.json()["session"]["phase"] == "stopped"
