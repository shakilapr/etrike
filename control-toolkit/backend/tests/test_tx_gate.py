"""TX gate policy: Bench TX, profile, ownership."""

from __future__ import annotations


def test_tx_gate_rejects_without_session_bench_tx(client):
    # No session
    r = client.post(
        "/api/v1/injections",
        json={
            "bus": "high",
            "key": "host:host_drive_cmd",
            "values": {"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
        },
    )
    assert r.status_code == 409

    # Session without TX
    client.post("/api/v1/sessions", json={"profile": "pure_software"})
    r = client.post(
        "/api/v1/injections",
        json={
            "bus": "high",
            "key": "host:host_drive_cmd",
            "values": {"speed_mmps": 0, "yaw_rate_mrad_s": 0, "gear": 0},
        },
    )
    assert r.status_code == 409
    assert r.json()["code"] == "bench_tx.disabled"


def test_tx_gate_accepts_pure_software_with_bench_tx(client):
    ses = client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()[
        "session"
    ]
    client.post(
        f"/api/v1/sessions/{ses['session_id']}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    )
    r = client.post(
        "/api/v1/injections",
        json={
            "bus": "high",
            "key": "host:host_drive_cmd",
            "values": {"speed_mmps": 10, "yaw_rate_mrad_s": 0, "gear": 1},
            "owner": "txgate:a",
        },
    )
    assert r.status_code == 200
    assert r.json()["disposition"] == "submitted"


def test_tx_gate_ownership_conflict(client):
    ses = client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()[
        "session"
    ]
    client.post(
        f"/api/v1/sessions/{ses['session_id']}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    )
    body = {
        "bus": "high",
        "key": "host:host_heartbeat",
        "values": {"alive_ctr": 1, "health_flags": 0},
        "owner": "owner-a",
    }
    assert client.post("/api/v1/injections", json=body).status_code == 200
    body["owner"] = "owner-b"
    r = client.post("/api/v1/injections", json=body)
    assert r.status_code == 409
    assert "ownership" in r.json()["code"] or "ownership" in r.json().get("detail", "")


def test_preview_does_not_require_bench_tx(client):
    r = client.post(
        "/api/v1/injections/preview",
        json={
            "bus": "high",
            "key": "hmi:hmi_mode_req",
            "values": {"req_mode": 1, "rolling_counter": 3},
        },
    )
    assert r.status_code == 200
    assert r.json()["ok"] is True
    assert r.json()["can_id"] == 0x111
    assert len(r.json()["data_hex"]) == 4  # DLC 2
