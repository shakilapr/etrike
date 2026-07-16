"""Sequential verification: stimulus → observe → Pass/Fail/Inconclusive."""

from __future__ import annotations

import time


def _enable_tx(client):
    ses = client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()[
        "session"
    ]
    client.post(
        f"/api/v1/sessions/{ses['session_id']}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    )
    return ses


def test_verification_pass_message_observed(client):
    _enable_tx(client)
    r = client.post(
        "/api/v1/tests",
        json={
            "name": "host_drive_observed",
            "stimulus": {
                "type": "inject",
                "bus": "high",
                "key": "host:host_drive_cmd",
                "values": {"speed_mmps": 250, "yaw_rate_mrad_s": 10, "gear": 1},
            },
            "expect": {
                "type": "message_observed",
                "bus": "high",
                "can_id": 0x300,
                "timeout_ms": 800,
            },
        },
    )
    assert r.status_code == 200, r.text
    body = r.json()["test"]
    assert body["disposition"] == "pass"
    assert body["test_id"].startswith("test_")
    got = client.get(f"/api/v1/tests/{body['test_id']}")
    assert got.status_code == 200
    assert got.json()["test"]["disposition"] == "pass"


def test_verification_signal_equals(client):
    _enable_tx(client)
    r = client.post(
        "/api/v1/tests",
        json={
            "name": "speed_equals",
            "stimulus": {
                "type": "inject",
                "bus": "high",
                "key": "host:host_drive_cmd",
                "values": {"speed_mmps": 400, "yaw_rate_mrad_s": 0, "gear": 1},
            },
            "expect": {
                "type": "signal_equals",
                "bus": "high",
                "can_id": 0x300,
                "signal": "speed_mmps",
                "equals": 400,
                "timeout_ms": 800,
            },
        },
    )
    assert r.status_code == 200, r.text
    assert r.json()["test"]["disposition"] == "pass"


def test_verification_fail_timeout(client):
    _enable_tx(client)
    r = client.post(
        "/api/v1/tests",
        json={
            "name": "missing_msg",
            "stimulus": {
                "type": "inject",
                "bus": "high",
                "key": "host:host_drive_cmd",
                "values": {"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
            },
            "expect": {
                "type": "message_observed",
                "bus": "high",
                "can_id": 0x7AB,  # not injected
                "timeout_ms": 80,
            },
        },
    )
    assert r.status_code == 200
    assert r.json()["test"]["disposition"] == "fail"


def test_verification_inconclusive_incomplete_evidence(client):
    _enable_tx(client)
    start = client.post("/api/v1/recordings")
    assert start.status_code == 200
    rid = start.json()["recording"]["recording_id"]

    # Force incomplete evidence via overflow path
    life = client.app.state.lifecycle
    rec = life.recording.active()
    assert rec is not None
    rec.capacity = 0  # next observe drops
    life.recording.observe_frame(
        bus="high",
        can_id=1,
        dlc=0,
        data=b"",
        direction="rx",
        source="virtual",
        backend_arrival_ns=time.time_ns(),
        adapter_epoch=1,
    )
    assert rec.evidence_quality.value == "incomplete"

    r = client.post(
        "/api/v1/tests",
        json={
            "name": "blocked_by_evidence",
            "stimulus": {
                "type": "inject",
                "bus": "high",
                "key": "host:host_drive_cmd",
                "values": {"speed_mmps": 1, "yaw_rate_mrad_s": 0, "gear": 1},
            },
            "expect": {
                "type": "message_observed",
                "bus": "high",
                "can_id": 0x300,
                "timeout_ms": 200,
            },
        },
    )
    assert r.status_code == 200
    body = r.json()["test"]
    assert body["disposition"] == "inconclusive"
    assert "evidence" in body["detail"].lower() or "complete" in body["detail"].lower()

    client.delete(f"/api/v1/recordings/{rid}")


def test_evidence_window_from_recording(client):
    _enable_tx(client)
    start = client.post("/api/v1/recordings")
    rid = start.json()["recording"]["recording_id"]
    client.post(
        "/api/v1/injections",
        json={
            "bus": "high",
            "key": "host:host_drive_cmd",
            "values": {"speed_mmps": 50, "yaw_rate_mrad_s": 0, "gear": 1},
            "owner": "ev:test",
        },
    )
    time.sleep(0.1)
    client.delete(f"/api/v1/recordings/{rid}")

    ev = client.get(f"/api/v1/evidence/{rid}?limit=50")
    assert ev.status_code == 200
    body = ev.json()
    assert body["evidence_id"] == rid
    assert body["kind"] == "recording"
    assert body["frame_total"] >= 1
    assert len(body["frames"]) >= 1

    exp = client.get(f"/api/v1/recordings/{rid}/export")
    assert exp.status_code == 200
    assert exp.json()["export_format"] == "control_toolkit.recording.v1"
