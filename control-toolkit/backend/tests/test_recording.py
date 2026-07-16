"""Recording start/stop integrity on virtual traffic."""

from __future__ import annotations

import time


def _tx(client):
    ses = client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()[
        "session"
    ]
    client.post(
        f"/api/v1/sessions/{ses['session_id']}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    )


def test_recording_start_stop_captures_frames(client):
    _tx(client)
    start = client.post("/api/v1/recordings")
    assert start.status_code == 200
    rid = start.json()["recording"]["recording_id"]
    assert start.json()["recording"]["state"] == "recording"
    assert client.get("/api/v1/sessions").json()["session"]["recording"] is True

    # inject traffic while recording
    client.post(
        "/api/v1/injections",
        json={
            "bus": "high",
            "key": "host:host_drive_cmd",
            "values": {"speed_mmps": 100, "yaw_rate_mrad_s": 0, "gear": 1},
            "owner": "rec:test",
        },
    )
    time.sleep(0.15)

    stop = client.delete(f"/api/v1/recordings/{rid}")
    assert stop.status_code == 200
    body = stop.json()["recording"]
    assert body["state"] == "stopped"
    assert body["frame_count"] >= 1
    assert body["evidence_quality"] in ("complete", "degraded", "incomplete")

    listed = client.get("/api/v1/recordings").json()
    assert listed["active"] is None
    assert any(r["recording_id"] == rid for r in listed["recordings"])

    detail = client.get(f"/api/v1/recordings/{rid}")
    assert detail.status_code == 200
    assert len(detail.json()["frames"]) >= 1


def test_recording_double_start_rejected(client):
    client.post("/api/v1/recordings")
    r = client.post("/api/v1/recordings")
    assert r.status_code == 409
    assert r.json()["code"] == "recording.active"
