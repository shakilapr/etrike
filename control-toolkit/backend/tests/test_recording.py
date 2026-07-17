"""Recording start/stop integrity on virtual traffic."""

from __future__ import annotations

import io
import time
import zipfile

import can


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


def test_vector_canalyzer_bundle_contains_readable_blf_dbc_and_sidecar(client):
    _tx(client)
    rid = client.post("/api/v1/recordings").json()["recording"]["recording_id"]
    client.post(
        "/api/v1/injections",
        json={
            "bus": "high",
            "key": "host:host_drive_cmd",
            "values": {"speed_mmps": 321, "yaw_rate_mrad_s": -50, "gear": 1},
            "owner": "rec:vector",
        },
    )
    time.sleep(0.15)
    client.delete(f"/api/v1/recordings/{rid}")

    response = client.get(f"/api/v1/recordings/{rid}/export/vector")
    assert response.status_code == 200
    assert response.headers["content-type"] == "application/zip"
    assert "canalyzer.zip" in response.headers["content-disposition"]

    with zipfile.ZipFile(io.BytesIO(response.content)) as bundle:
        names = set(bundle.namelist())
        assert f"{rid}.blf" in names
        assert f"{rid}.metadata.json" in names
        assert "dbc/etrike_high.dbc" in names
        assert "dbc/etrike_low.dbc" in names
        assert "README.txt" in names
        blf = bundle.read(f"{rid}.blf")

    # BLF is not merely present: python-can can parse the generated Vector log.
    class _ReadableBuffer(io.BytesIO):
        @property
        def name(self) -> str:
            return "recording.blf"

    messages = list(can.BLFReader(_ReadableBuffer(blf)))
    assert messages
    frame = next(msg for msg in messages if msg.arbitration_id == 0x300)
    assert frame.channel == 0
    assert frame.dlc == 8
