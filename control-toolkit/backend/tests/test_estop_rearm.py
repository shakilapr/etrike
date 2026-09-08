"""Bench REARM endpoint emits the SYS-side clear + REARM sequence frames."""


def _session(client):
    return client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()[
        "session"
    ]


def _enable_bench(client, sid, rev):
    r = client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": rev},
    )
    return r.json()["session"]


def test_rearm_requires_bench_tx(client):
    client.post("/api/v1/sessions", json={"profile": "pure_software"})
    r = client.post("/api/v1/control/estop/rearm")
    assert r.status_code == 409


def test_rearm_emits_clear_and_rearm_sequence(client):
    ses = _session(client)
    ses = _enable_bench(client, ses["session_id"], ses["revision"])

    r = client.post("/api/v1/control/estop/rearm")
    assert r.status_code == 200, r.text
    body = r.json()
    tx = body["tx"]
    keys = [item["key"] for item in tx]
    # Two advancing 0x011 clear frames, then 0x110 MANUAL, then 0x113 OFF->ON.
    assert keys.count("sys:sys_safety_sts") == 2
    assert keys == [
        "sys:sys_safety_sts",
        "sys:sys_safety_sts",
        "sys:sys_mode_cmd",
        "sys:sys_pwr_cmd",
        "sys:sys_pwr_cmd",
    ]
    assert all(item["disposition"] == "submitted" for item in tx), tx
    assert body["estop"]["active"] is False
    assert body["session"]["estop_active"] is False
