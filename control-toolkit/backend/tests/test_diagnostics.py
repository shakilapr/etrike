"""Diagnostic events and episode aggregation."""

from __future__ import annotations


def test_backend_ready_event_on_startup(client):
    r = client.get("/api/v1/events")
    assert r.status_code == 200
    codes = {e["code"] for e in r.json()["events"]}
    assert "backend.ready" in codes


def test_stop_all_emits_event(client):
    ses = client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()[
        "session"
    ]
    sid = ses["session_id"]
    ses = client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    ).json()["session"]
    client.post(
        f"/api/v1/sessions/{sid}/stop-all",
        json={"expected_revision": ses["revision"]},
    )
    events = client.get("/api/v1/events?code=session.stop_all").json()["events"]
    assert events
    assert events[0]["code"] == "session.stop_all"


def test_episode_aggregation_for_repeated_warnings(client):
    diag = client.app.state.lifecycle.diagnostics
    for i in range(5):
        diag.emit(
            code="frame.invalid",
            title="Invalid frame",
            detail=f"n={i}",
            severity="warning",
            bus="high",
            can_id=0x300,
        )
    episodes = client.get("/api/v1/episodes").json()["episodes"]
    inv = [e for e in episodes if e["code"] == "frame.invalid"]
    assert inv
    assert inv[0]["count"] >= 5
    assert inv[0]["recovered"] is False

    # Hysteresis: first recover arms; force commits immediately for test.
    assert diag.recover("frame.invalid", scope="high") is False
    assert diag.recover("frame.invalid", scope="high", force=True) is True
    episodes = client.get("/api/v1/episodes").json()["episodes"]
    inv = [e for e in episodes if e["code"] == "frame.invalid"]
    assert inv[0]["recovered"] is True


def test_get_event_by_id(client):
    events = client.get("/api/v1/events?limit=5").json()["events"]
    assert events
    eid = events[0]["event_id"]
    r = client.get(f"/api/v1/events/{eid}")
    assert r.status_code == 200
    assert r.json()["event_id"] == eid
    assert client.get("/api/v1/events/evt_missing").status_code == 404
