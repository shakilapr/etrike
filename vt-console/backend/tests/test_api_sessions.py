"""Session API surface (workplan §3.5)."""

from __future__ import annotations


def test_get_sessions_empty(client):
    r = client.get("/api/v1/sessions")
    assert r.status_code == 200
    assert r.json()["session"]["session_id"] is None


def test_status_includes_session_fields(client):
    client.post("/api/v1/sessions", json={"profile": "pure_software"})
    st = client.get("/api/v1/status").json()
    assert st["session"]["phase"] == "running"
    assert st["session"]["destination"] == "virtual"


def test_create_then_get_reflects_same_session(client):
    created = client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()["session"]
    fetched = client.get("/api/v1/sessions").json()["session"]
    assert fetched["session_id"] == created["session_id"]
    assert fetched["revision"] == created["revision"]
