"""Negative and boundary coverage for the public Control Toolkit API."""

from __future__ import annotations

import io
import zipfile


def test_protocol_and_query_validation(client):
    assert client.get("/api/v1/protocol/messages/sideways/0x300").status_code == 404
    assert client.get("/api/v1/protocol/messages/high/not-an-id").status_code == 400
    assert client.get("/api/v1/evidence/missing?limit=0").status_code == 422
    assert client.get("/api/v1/evidence/missing?limit=5001").status_code == 422
    assert client.get("/api/v1/events?limit=0").status_code == 422


def test_missing_resources_return_structured_404(client):
    for path in (
        "/api/v1/events/no-such-event",
        "/api/v1/evidence/no-such-evidence",
        "/api/v1/recordings/no-such-recording",
        "/api/v1/recordings/no-such-recording/export",
        "/api/v1/recordings/no-such-recording/export/vector",
    ):
        response = client.get(path)
        assert response.status_code == 404
        assert response.json()


def test_recording_exports_and_evidence_window(client):
    started = client.post("/api/v1/recordings")
    assert started.status_code == 200
    recording_id = started.json()["recording"]["recording_id"]
    assert client.get("/api/v1/recordings").json()["active"] is not None

    stopped = client.delete(f"/api/v1/recordings/{recording_id}")
    assert stopped.status_code == 200
    assert stopped.json()["recording"]["recording_id"] == recording_id

    detail = client.get(f"/api/v1/recordings/{recording_id}")
    assert detail.status_code == 200
    exported = client.get(f"/api/v1/recordings/{recording_id}/export")
    assert exported.status_code == 200
    assert exported.json()["recording_id"] == recording_id

    bundle = client.get(f"/api/v1/recordings/{recording_id}/export/vector")
    assert bundle.status_code == 200
    with zipfile.ZipFile(io.BytesIO(bundle.content)) as archive:
        assert f"{recording_id}.blf" in archive.namelist()
        assert "README.txt" in archive.namelist()

    evidence = client.get(f"/api/v1/evidence/{recording_id}?limit=1&offset=0")
    assert evidence.status_code == 200
    assert evidence.json()["evidence_id"] == recording_id


def test_recording_cannot_start_twice_and_stop_is_idempotently_rejected(client):
    first = client.post("/api/v1/recordings")
    assert first.status_code == 200
    second = client.post("/api/v1/recordings")
    assert second.status_code == 409
    recording_id = first.json()["recording"]["recording_id"]
    assert client.delete(f"/api/v1/recordings/{recording_id}").status_code == 200
    assert client.delete(f"/api/v1/recordings/{recording_id}").status_code == 404


def test_session_and_control_invalid_inputs_are_rejected(client):
    assert client.post("/api/v1/sessions", json={"profile": "not-a-profile"}).status_code in {
        400,
        422,
    }
    assert client.post("/api/v1/hmi/mode", json={"req_mode": 99, "enabled": True}).status_code in {
        400,
        422,
    }
    assert client.post("/api/v1/control/direct", json={"actuator": "unknown"}).status_code in {
        400,
        422,
    }
