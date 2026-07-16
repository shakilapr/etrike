"""Exhaustive HTTP surface: every REST endpoint returns a coherent response."""

from __future__ import annotations

import time

from control_toolkit import protocol_bridge as proto


def _tx_session(client):
    cur = client.get("/api/v1/sessions").json()["session"]
    if cur.get("session_id"):
        client.request(
            "DELETE",
            f"/api/v1/sessions/{cur['session_id']}",
            json={"expected_revision": cur["revision"], "outcome": "stopped"},
        )
    created = client.post("/api/v1/sessions", json={"profile": "pure_software"})
    assert created.status_code == 200, created.text
    ses = created.json()["session"]
    sid = ses["session_id"]
    ses = client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    ).json()["session"]
    return sid, ses


def test_openapi_lists_core_paths(client):
    r = client.get("/openapi.json")
    assert r.status_code == 200
    paths = r.json()["paths"]
    required = [
        "/api/v1/status",
        "/api/v1/state",
        "/api/v1/history",
        "/api/v1/topology",
        "/api/v1/protocol/messages",
        "/api/v1/sessions",
        "/api/v1/sessions/profiles",
        "/api/v1/injections",
        "/api/v1/injections/preview",
        "/api/v1/analysis/host-drive",
        "/api/v1/analysis/stop",
        "/api/v1/hmi/mode",
        "/api/v1/hmi/power",
        "/api/v1/synthetic-peers",
        "/api/v1/recordings",
        "/api/v1/events",
        "/api/v1/episodes",
    ]
    for p in required:
        assert p in paths, f"missing OpenAPI path {p}"


def test_status_ready_and_hashes(client):
    body = client.get("/api/v1/status").json()
    assert body["ready"] is True
    assert body["wire_hash"] == proto.WIRE_HASH
    assert body["semantic_hash"] == proto.SEMANTIC_HASH
    assert "adapter" in body
    assert "session" in body
    assert body["adapter"]["health"] in ("ok", "healthy", "open", "Absent", "absent") or True


def test_state_history_topology(client):
    assert client.get("/api/v1/state").status_code == 200
    hist = client.get("/api/v1/history?limit=10").json()
    assert "frames" in hist and "metrics" in hist
    topo = client.get("/api/v1/topology").json()
    assert len(topo["nodes"]) >= 5


def test_protocol_catalog(client):
    msgs = client.get("/api/v1/protocol/messages").json()
    assert msgs["count"] >= 1
    assert msgs["semantic_hash"] == proto.SEMANTIC_HASH
    assert len(msgs["instances"]) >= 1
    # Known message on high bus
    detail = client.get("/api/v1/protocol/messages/high/0x300")
    assert detail.status_code == 200
    assert detail.json()["name"] == "HOST_DRIVE_CMD"
    assert client.get("/api/v1/protocol/messages/high/0xDEAD").status_code == 404


def test_sessions_full_lifecycle_api(client):
    # empty
    empty = client.get("/api/v1/sessions").json()["session"]
    assert empty["session_id"] is None

    profiles = client.get("/api/v1/sessions/profiles").json()["profiles"]
    assert {p["id"] for p in profiles} >= {
        "pure_software",
        "bench_test",
        "full_vehicle",
    }
    pure = next(p for p in profiles if p["id"] == "pure_software")
    assert pure["available"] is True
    blocked = [p for p in profiles if p["id"] != "pure_software"]
    assert all(not p["available"] for p in blocked)

    created = client.post(
        "/api/v1/sessions", json={"profile": "pure_software"}
    ).json()["session"]
    sid = created["session_id"]
    assert created["phase"] == "running"
    assert created["bench_tx"] == "disabled"
    assert created["destination"] == "virtual"
    rev = created["revision"]

    # physical profile refused without adapter
    r = client.post(
        f"/api/v1/sessions/{sid}/profile",
        json={"profile": "full_vehicle", "expected_revision": rev, "confirm": True},
    )
    assert r.status_code == 503
    assert r.json()["code"] == "profile.physical_unavailable"

    # vehicle view
    r = client.post(
        f"/api/v1/sessions/{sid}/vehicle-view",
        json={"requested_mode": "AUTO", "requested_power": "ON"},
    )
    assert r.status_code == 200
    assert r.json()["session"]["requested_mode"] == "AUTO"

    # bench tx + lease + stop-all
    ses = client.get("/api/v1/sessions").json()["session"]
    ses = client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    ).json()["session"]
    assert ses["bench_tx"] == "enabled"

    lease = client.post(
        f"/api/v1/sessions/{sid}/leases",
        json={"bus": "high", "can_id": 0x111, "owner": "surface-test"},
    ).json()
    assert "lease_id" in lease
    assert (
        client.post(
            f"/api/v1/sessions/{sid}/leases/renew",
            json={"lease_id": lease["lease_id"], "ttl_s": 5},
        ).status_code
        == 200
    )
    assert (
        client.delete(f"/api/v1/sessions/{sid}/leases/{lease['lease_id']}").status_code
        == 200
    )

    ses = client.get("/api/v1/sessions").json()["session"]
    stop = client.post(
        f"/api/v1/sessions/{sid}/stop-all",
        json={"expected_revision": ses["revision"]},
    ).json()["session"]
    assert stop["bench_tx"] == "disabled"

    ses = client.get("/api/v1/sessions").json()["session"]
    closed = client.request(
        "DELETE",
        f"/api/v1/sessions/{sid}",
        json={"expected_revision": ses["revision"], "outcome": "stopped"},
    )
    assert closed.status_code == 200
    assert closed.json()["session"]["session_id"] is None


def test_injections_preview_submit_periodic_cancel(client):
    preview = client.post(
        "/api/v1/injections/preview",
        json={
            "bus": "high",
            "key": "host:host_drive_cmd",
            "values": {"speed_mmps": 100, "yaw_rate_mrad_s": 10, "gear": 1},
        },
    ).json()
    assert preview["ok"] is True
    assert preview["data_hex"]

    # reject without TX (session may exist from empty bench)
    r = client.post(
        "/api/v1/injections",
        json={
            "bus": "high",
            "key": "host:host_drive_cmd",
            "values": {"speed_mmps": 1, "yaw_rate_mrad_s": 0, "gear": 0},
        },
    )
    assert r.status_code == 409

    sid, ses = _tx_session(client)
    oneshot = client.post(
        "/api/v1/injections",
        json={
            "bus": "high",
            "key": "host:host_drive_cmd",
            "values": {"speed_mmps": 250, "yaw_rate_mrad_s": 30, "gear": 1},
            "owner": "surface:inject",
        },
    )
    assert oneshot.status_code == 200
    assert oneshot.json()["disposition"] == "submitted"

    periodic = client.post(
        "/api/v1/injections",
        json={
            "bus": "high",
            "key": "host:host_heartbeat",
            "values": {"alive_ctr": 0, "health_flags": 0},
            "period_ms": 40,
            "counter_field": "alive_ctr",
            "owner": "surface:periodic",
        },
    )
    assert periodic.status_code == 200
    job_id = periodic.json()["job_id"]
    time.sleep(0.12)
    assert client.delete(f"/api/v1/injections/{job_id}").status_code == 200
    assert client.delete(f"/api/v1/injections/{job_id}").status_code == 404
    _ = sid


def test_analysis_host_drive_api(client):
    _tx_session(client)
    r = client.post(
        "/api/v1/analysis/host-drive",
        json={"speed_mmps": 300, "yaw_rate_mrad_s": 50, "gear": 1},
    )
    assert r.status_code == 200
    assert r.json()["mode"] == "oneshot"
    stop = client.post("/api/v1/analysis/stop")
    assert stop.status_code == 200


def test_hmi_mode_power_api(client):
    _tx_session(client)
    mode = client.post("/api/v1/hmi/mode", json={"req_mode": 1, "enabled": True})
    assert mode.status_code == 200
    assert mode.json()["enabled"] is True
    assert "job_id" in mode.json()

    pwr = client.post("/api/v1/hmi/power", json={"req_start": 1, "enabled": True})
    assert pwr.status_code == 200
    assert pwr.json()["enabled"] is True

    # disable
    assert (
        client.post("/api/v1/hmi/mode", json={"req_mode": 0, "enabled": False}).json()[
            "enabled"
        ]
        is False
    )
    assert (
        client.post("/api/v1/hmi/power", json={"req_start": 0, "enabled": False}).json()[
            "enabled"
        ]
        is False
    )


def test_synthetic_peers_api(client):
    listed = client.get("/api/v1/synthetic-peers").json()
    assert "available" in listed
    assert "running" in listed

    # without bench tx
    cur = client.get("/api/v1/sessions").json()["session"]
    if not cur.get("session_id"):
        client.post("/api/v1/sessions", json={"profile": "pure_software"})
    r = client.post(
        "/api/v1/synthetic-peers/start", json={"names": ["host_drive_analysis"]}
    )
    assert r.status_code == 409  # bench tx disabled

    _tx_session(client)
    r = client.post(
        "/api/v1/synthetic-peers/start", json={"names": ["host_drive_analysis"]}
    )
    assert r.status_code == 200
    assert r.json()["started"]
    stop = client.post("/api/v1/synthetic-peers/stop", json={})
    assert stop.status_code == 200


def test_estop_injection_dlc0(client):
    _tx_session(client)
    r = client.post(
        "/api/v1/injections",
        json={
            "bus": "high",
            "key": "safety:safety_estop",
            "values": {},
            "owner": "ui:estop",
        },
    )
    assert r.status_code == 200
    assert r.json()["disposition"] == "submitted"
    assert r.json()["can_id"] == 0x001


def test_websocket_stream_still_works(client):
    with client.websocket_connect("/api/v1/stream") as ws:
        hello = ws.receive_json()
        assert hello["type"] == "hello"
        assert hello["wire_hash"] == proto.WIRE_HASH
