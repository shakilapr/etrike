"""Injection encode/TX and observation on virtual buses."""

from __future__ import annotations

import time


def _session_with_tx(client):
    ses = client.post(
        "/api/v1/sessions", json={"profile": "pure_software"}
    ).json()["session"]
    sid = ses["session_id"]
    ses = client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    ).json()["session"]
    return sid, ses


def test_preview_host_drive_cmd(client):
    r = client.post(
        "/api/v1/injections/preview",
        json={
            "bus": "high",
            "key": "host:host_drive_cmd",
            "values": {"speed_mmps": 500, "yaw_rate_mrad_s": 0, "gear": 1},
        },
    )
    assert r.status_code == 200
    body = r.json()
    assert body["ok"] is True
    assert body["name"] == "HOST_DRIVE_CMD"
    assert body["dlc"] == 8
    assert len(body["data_hex"]) == 16


def test_inject_requires_bench_tx(client):
    client.post("/api/v1/sessions", json={"profile": "pure_software"})
    r = client.post(
        "/api/v1/injections",
        json={
            "bus": "high",
            "key": "host:host_drive_cmd",
            "values": {"speed_mmps": 100, "yaw_rate_mrad_s": 0, "gear": 0},
        },
    )
    assert r.status_code == 409
    assert r.json()["code"] == "bench_tx.disabled"


def test_oneshot_inject_appears_in_state(client):
    _session_with_tx(client)
    r = client.post(
        "/api/v1/injections",
        json={
            "bus": "low",
            "key": "sys:sys_heartbeat",
            "values": {
                "alive_ctr": 7,
                "heartbeat_ok": 1,
                "estop_active": 0,
                "mode_auto": 0,
                "can_ok": 1,
                "task_safety_ok": 1,
                "task_brake_ok": 1,
                "task_dispatch_ok": 1,
                "task_can_tx_ok": 1,
            },
        },
    )
    assert r.status_code == 200
    assert r.json()["disposition"] == "submitted"

    deadline = time.time() + 2.0
    found = None
    while time.time() < deadline:
        msgs = client.get("/api/v1/state").json()["messages"]
        found = next(
            (m for m in msgs if m["bus"] == "low" and m["can_id"] == 0x7FE),
            None,
        )
        if found and found.get("signals"):
            break
        time.sleep(0.05)
    assert found is not None
    assert found["name"] == "SYS_HEARTBEAT"
    assert found["validation_status"] == "ok"
    assert found["signals"]["alive_ctr"]["engineering_value"] == 7


def test_periodic_inject_and_cancel(client):
    _session_with_tx(client)
    r = client.post(
        "/api/v1/injections",
        json={
            "bus": "high",
            "key": "host:host_heartbeat",
            "values": {"alive_ctr": 0, "health_flags": 0},
            "period_ms": 50,
            "counter_field": "alive_ctr",
        },
    )
    assert r.status_code == 200
    job_id = r.json()["job_id"]
    time.sleep(0.2)
    r = client.delete(f"/api/v1/injections/{job_id}")
    assert r.status_code == 200
    assert r.json()["canceled"] is True


def test_ownership_conflict(client):
    _session_with_tx(client)
    body = {
        "bus": "high",
        "key": "host:host_heartbeat",
        "values": {"alive_ctr": 1, "health_flags": 0},
        "owner": "a",
    }
    assert client.post("/api/v1/injections", json=body).status_code == 200
    body["owner"] = "b"
    r = client.post("/api/v1/injections", json=body)
    assert r.status_code == 409


def test_list_and_cancel_all_jobs(client):
    _session_with_tx(client)
    r1 = client.post(
        "/api/v1/injections",
        json={
            "bus": "high",
            "key": "host:host_heartbeat",
            "values": {"alive_ctr": 0, "health_flags": 0},
            "period_ms": 100,
        },
    )
    assert r1.status_code == 200
    r2 = client.get("/api/v1/injections/jobs")
    assert r2.status_code == 200
    jobs = r2.json()["jobs"]
    assert len(jobs) >= 1
    assert any(j["job_id"] == r1.json()["job_id"] for j in jobs)

    r3 = client.delete("/api/v1/injections")
    assert r3.status_code == 200
    assert r3.json()["canceled_count"] >= 1

    r4 = client.get("/api/v1/injections/jobs")
    assert len(r4.json()["jobs"]) == 0

