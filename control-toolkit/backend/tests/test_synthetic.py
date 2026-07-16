"""Synthetic peer start/stop."""

from __future__ import annotations

import time


def test_list_and_start_stop_peers(client):
    ses = client.post(
        "/api/v1/sessions", json={"profile": "pure_software"}
    ).json()["session"]
    sid = ses["session_id"]
    client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    )

    listed = client.get("/api/v1/synthetic-peers").json()
    assert len(listed["available"]) >= 4

    started = client.post(
        "/api/v1/synthetic-peers/start",
        json={"names": ["host_heartbeat", "sys_heartbeat"]},
    ).json()["started"]
    assert len(started) == 2

    time.sleep(0.25)
    msgs = client.get("/api/v1/state").json()["messages"]
    names = {m["name"] for m in msgs}
    assert "HOST_HEARTBEAT" in names or "SYS_HEARTBEAT" in names

    stopped = client.post("/api/v1/synthetic-peers/stop", json={}).json()
    assert stopped["stopped"] >= 1


def test_start_requires_bench_tx(client):
    client.post("/api/v1/sessions", json={"profile": "pure_software"})
    r = client.post("/api/v1/synthetic-peers/start", json={})
    assert r.status_code == 409
