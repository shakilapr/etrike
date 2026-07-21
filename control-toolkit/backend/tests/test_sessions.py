"""Session, profile, Bench TX, Stop All, ownership."""

from __future__ import annotations

import time


def _create_session(client, profile: str = "pure_software"):
    r = client.post("/api/v1/sessions", json={"profile": profile})
    return r


def test_create_pure_software_session(client):
    r = _create_session(client)
    assert r.status_code == 200
    ses = r.json()["session"]
    assert ses["session_id"]
    assert ses["profile"] == "pure_software"
    assert ses["phase"] == "running"
    assert ses["bench_tx"] == "disabled"
    assert ses["revision"] >= 1


def test_physical_profile_without_adapter(client):
    """Real mode is allowed without CANalyst; link is absent and TX stays off."""
    r = _create_session(client, "full_vehicle")
    assert r.status_code == 200
    body = r.json()["session"]
    assert body["profile"] == "full_vehicle"
    assert body["destination"] == "physical"
    assert body["bench_tx"] == "disabled"
    st = client.get("/api/v1/status").json()
    assert st["adapter"]["health"] == "absent"
    assert st["link"]["connected"] is False


def test_stop_all_clears_estop_latch(client):
    ses = _create_session(client).json()["session"]
    sid = ses["session_id"]
    client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    )
    client.post(
        "/api/v1/control/intent",
        json={
            "sequence": 1,
            "source": "test",
            "mode": "estop",
            "throttle": 0,
            "steer": 0,
            "estop": True,
        },
    )
    assert client.get("/api/v1/status").json()["session"]["estop_active"] is True
    ses = client.get("/api/v1/sessions").json()["session"]
    r = client.post(
        f"/api/v1/sessions/{sid}/stop-all",
        json={"expected_revision": ses["revision"]},
    )
    assert r.status_code == 200
    assert r.json()["session"]["estop_active"] is False
    assert r.json()["session"]["bench_tx"] == "disabled"


def test_clear_estop_endpoint(client):
    ses = _create_session(client).json()["session"]
    sid = ses["session_id"]
    client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    )
    client.post(
        "/api/v1/control/intent",
        json={
            "sequence": 1,
            "source": "test",
            "mode": "estop",
            "throttle": 0,
            "steer": 0,
            "estop": True,
        },
    )
    r = client.post("/api/v1/control/estop/clear", json={})
    assert r.status_code == 200
    assert r.json()["session"]["estop_active"] is False
    assert client.get("/api/v1/status").json()["session"]["estop_active"] is False


def test_bench_tx_enable_disable(client):
    ses = _create_session(client).json()["session"]
    sid = ses["session_id"]
    r = client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    )
    assert r.status_code == 200
    assert r.json()["session"]["bench_tx"] == "enabled"
    rev = r.json()["session"]["revision"]
    r = client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": False, "expected_revision": rev},
    )
    assert r.json()["session"]["bench_tx"] == "disabled"


def test_revision_conflict(client):
    ses = _create_session(client).json()["session"]
    sid = ses["session_id"]
    r = client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": 999},
    )
    assert r.status_code == 409
    assert r.json()["code"] == "session.revision_conflict"


def test_stop_all_disables_tx(client):
    ses = _create_session(client).json()["session"]
    sid = ses["session_id"]
    client.post(
        f"/api/v1/sessions/{sid}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    )
    # get latest revision
    ses = client.get("/api/v1/sessions").json()["session"]
    r = client.post(
        f"/api/v1/sessions/{sid}/stop-all",
        json={"expected_revision": ses["revision"]},
    )
    assert r.status_code == 200
    assert r.json()["session"]["bench_tx"] == "disabled"


def test_close_session(client):
    ses = _create_session(client).json()["session"]
    sid = ses["session_id"]
    r = client.request(
        "DELETE",
        f"/api/v1/sessions/{sid}",
        json={"expected_revision": ses["revision"]},
    )
    assert r.status_code == 200
    assert r.json()["session"]["session_id"] is None
    assert r.json()["session"]["phase"] == "stopped"


def test_status_reflects_session(client):
    _create_session(client)
    st = client.get("/api/v1/status").json()
    assert st["session"]["session_id"] is not None
