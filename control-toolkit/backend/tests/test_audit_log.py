"""Operational audit log API."""

from __future__ import annotations


def test_logs_on_startup_and_session(client):
    r = client.get("/api/v1/logs?limit=50")
    assert r.status_code == 200
    body = r.json()
    assert "logs" in body
    assert body["stats"]["count"] >= 1
    codes = {e["code"] for e in body["logs"]}
    assert "backend.ready" in codes or "transport.virtual_open" in codes


def test_logs_filter_and_inject(client):
    ses = client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()[
        "session"
    ]
    client.post(
        f"/api/v1/sessions/{ses['session_id']}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    )
    client.post(
        "/api/v1/injections",
        json={
            "bus": "high",
            "key": "host:host_drive_cmd",
            "values": {"speed_mmps": 1, "yaw_rate_mrad_s": 0, "gear": 0},
            "owner": "log:test",
        },
    )
    r = client.get("/api/v1/logs?category=inject&limit=20")
    assert r.status_code == 200
    assert any(e["code"] == "inject.submitted" for e in r.json()["logs"])

    r2 = client.get("/api/v1/logs?q=session&limit=50")
    assert r2.status_code == 200
    assert r2.json()["count"] >= 1


def test_logs_clear(client):
    client.get("/api/v1/logs")
    r = client.delete("/api/v1/logs")
    assert r.status_code == 200
    assert r.json()["cleared"] >= 0
    # clear itself logs an entry
    after = client.get("/api/v1/logs").json()
    assert after["stats"]["count"] >= 1
    assert any(e["code"] == "log.cleared" for e in after["logs"])
