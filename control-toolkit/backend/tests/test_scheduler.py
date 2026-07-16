"""Periodic scheduler: re-encode, cancel, skip burst."""

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


def test_scheduler_periodic_counter_advances(client):
    _tx(client)
    r = client.post(
        "/api/v1/injections",
        json={
            "bus": "high",
            "key": "host:host_heartbeat",
            "values": {"alive_ctr": 0, "health_flags": 0},
            "period_ms": 50,
            "counter_field": "alive_ctr",
            "owner": "sched:hb",
        },
    )
    assert r.status_code == 200
    job_id = r.json()["job_id"]
    assert job_id in client.app.state.lifecycle.scheduler.job_ids()

    deadline = time.time() + 2.0
    last_ctr = None
    saw_advance = False
    while time.time() < deadline:
        msgs = client.get("/api/v1/state").json()["messages"]
        hb = next((m for m in msgs if m.get("name") == "HOST_HEARTBEAT"), None)
        if hb and hb.get("signals"):
            ctr = int(hb["signals"]["alive_ctr"]["engineering_value"])
            if last_ctr is not None and ctr != last_ctr:
                saw_advance = True
                break
            last_ctr = ctr
        time.sleep(0.05)
    assert saw_advance, "rolling counter never advanced"
    assert client.delete(f"/api/v1/injections/{job_id}").status_code == 200
    assert job_id not in client.app.state.lifecycle.scheduler.job_ids()


def test_stop_all_cancels_jobs(client):
    ses = client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()[
        "session"
    ]
    sid = ses["session_id"]
    ses = client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    ).json()["session"]
    client.post(
        "/api/v1/injections",
        json={
            "bus": "high",
            "key": "host:host_heartbeat",
            "values": {"alive_ctr": 0, "health_flags": 0},
            "period_ms": 40,
            "counter_field": "alive_ctr",
            "owner": "sched:stop",
        },
    )
    assert client.app.state.lifecycle.scheduler.job_ids()
    ses = client.get("/api/v1/sessions").json()["session"]
    r = client.post(
        f"/api/v1/sessions/{sid}/stop-all",
        json={"expected_revision": ses["revision"]},
    )
    assert r.status_code == 200
    assert client.app.state.lifecycle.scheduler.job_ids() == []
    assert client.get("/api/v1/sessions").json()["session"]["bench_tx"] == "disabled"
