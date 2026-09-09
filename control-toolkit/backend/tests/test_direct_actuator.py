"""Direct low-bus actuator streams (motor / steering / brake)."""

from __future__ import annotations

import json
import time
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from control_toolkit.config import ToolkitConfig
from control_toolkit.main import create_app
from control_toolkit.models.frames import ChannelId
from control_toolkit.services.native_sil import sil_input_for


def _tx(client):
    cur = client.get("/api/v1/sessions").json()["session"]
    if cur.get("session_id"):
        client.request(
            "DELETE",
            f"/api/v1/sessions/{cur['session_id']}",
            json={"expected_revision": cur["revision"]},
        )
    ses = client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()[
        "session"
    ]
    client.post(
        f"/api/v1/sessions/{ses['session_id']}/bench-tx",
        json={"enabled": True, "expected_revision": ses["revision"]},
    )


def test_direct_motor_appears_on_low_bus(client):
    _tx(client)
    r = client.post(
        "/api/v1/control/direct",
        json={
            "channel": "motor",
            "enabled": True,
            "values": {"motor_speed_mmps": 400, "gear": 1},
        },
    )
    assert r.status_code == 200
    assert r.json()["control"]["mode"] == "direct"
    assert "motor" in r.json()["control"]["direct_channels"]

    deadline = time.time() + 2.0
    found = None
    while time.time() < deadline:
        msgs = client.get("/api/v1/state").json()["messages"]
        found = next(
            (m for m in msgs if m.get("name") == "RT_DRIVE_CMD" or m.get("can_id") == 0x204),
            None,
        )
        if found and found.get("signals"):
            break
        time.sleep(0.03)
    assert found is not None
    assert found["bus"] == "low"
    assert int(found["signals"]["motor_speed_mmps"]["engineering_value"]) == 400


def test_direct_steering_and_brake_encode(client):
    _tx(client)
    steer = client.post(
        "/api/v1/control/direct",
        json={
            "channel": "steering",
            "enabled": True,
            "values": {
                "target_angle_raw": 50,
                "control_enable": True,
                "alignment_enable": True,
            },
        },
    )
    assert steer.status_code == 200
    brake = client.post(
        "/api/v1/control/direct",
        json={
            "channel": "brake",
            "enabled": True,
            "values": {"pressure_request_raw": 40, "control_enable": True},
        },
    )
    assert brake.status_code == 200
    ch = set(brake.json()["control"]["direct_channels"])
    assert {"steering", "brake"} <= ch

    deadline = time.time() + 2.0
    names = set()
    while time.time() < deadline:
        msgs = client.get("/api/v1/state").json()["messages"]
        names = {m.get("name") for m in msgs}
        if "VCU_SES_REQ" in names and "VCU_SEB_REQ" in names:
            break
        time.sleep(0.03)
    assert "VCU_SES_REQ" in names
    assert "VCU_SEB_REQ" in names


def test_kinematics_and_direct_mutual_exclusion(client):
    _tx(client)
    client.post(
        "/api/v1/control/intent",
        json={"sequence": 1, "throttle": 0.4, "steer": 0, "mode": "kinematics"},
    )
    assert client.get("/api/v1/control/status").json()["control"]["job_id"]

    client.post(
        "/api/v1/control/direct",
        json={
            "channel": "motor",
            "enabled": True,
            "values": {"motor_speed_mmps": 100, "gear": 1},
        },
    )
    st = client.get("/api/v1/control/status").json()["control"]
    assert st["mode"] == "direct"
    assert st["job_id"] is None  # kinematics cancelled
    assert "motor" in st["direct_channels"]

    # Kinematics again clears direct
    client.post(
        "/api/v1/control/intent",
        json={"sequence": 2, "throttle": 0.1, "steer": 0, "mode": "kinematics"},
    )
    st = client.get("/api/v1/control/status").json()["control"]
    assert st["mode"] == "kinematics"
    assert st["direct_channels"] == []
    assert st["job_id"]


def test_direct_stop_channel(client):
    _tx(client)
    client.post(
        "/api/v1/control/direct",
        json={
            "channel": "motor",
            "enabled": True,
            "values": {"motor_speed_mmps": 50, "gear": 1},
        },
    )
    r = client.post(
        "/api/v1/control/direct",
        json={"channel": "motor", "enabled": False},
    )
    assert r.status_code == 200
    assert "motor" not in r.json()["control"]["direct_channels"]


def test_native_sil_receives_low_direct_motor_command(client):
    """Low direct commands must reach the native RT simulation, not just UI."""
    exe = Path(__file__).parents[3] / "native-test" / "build-sil" / "sim_engine_native.exe"
    if not exe.is_file():
        pytest.skip("build native-test/build-sil/sim_engine_native before running SIL tests")

    app = create_app(ToolkitConfig(native_sil_executable=str(exe)))
    with TestClient(app) as sil_client:
        lifecycle = app.state.lifecycle
        assert lifecycle.native_sil is not None
        assert lifecycle.native_sil.running
        _tx(sil_client)
        _verify_low_direct_reaches_bridge(sil_client, lifecycle)


def _verify_low_direct_reaches_bridge(client, lifecycle) -> None:
    if lifecycle.native_sil is None:
        pytest.fail("native SIL bridge is not running")
    try:
        bridge_count = lifecycle.native_sil.low_input_count
        low_frame = None
        transport = lifecycle.transport
        original_send = transport.send

        def capture_send(frame):
            nonlocal low_frame
            if frame.channel is ChannelId.LOW and frame.can_id == 0x204:
                low_frame = frame
            return original_send(frame)

        transport.send = capture_send
        try:
            response = client.post(
                "/api/v1/control/direct",
                json={
                    "channel": "motor",
                    "enabled": True,
                    "values": {"motor_speed_mmps": 400, "gear": 1},
                },
            )
            assert response.status_code == 200, response.text
            deadline = time.monotonic() + 2.0
            while low_frame is None and time.monotonic() < deadline:
                time.sleep(0.01)
        finally:
            transport.send = original_send
        assert low_frame is not None

        expected_input = {
            "type": "frame",
            "bus": "low",
            "id": "0x204",
            "data": list(low_frame.data),
        }
        parsed = json.loads(sil_input_for(low_frame))
        assert parsed == expected_input
        assert lifecycle.native_sil.low_input_count > bridge_count
    finally:
        client.post("/api/v1/control/direct", json={"channel": "motor", "enabled": False})


def test_direct_request_rejects_unknown_body_fields(client):
    response = client.post(
        "/api/v1/control/direct",
        json={"channel": "motor", "enabled": True, "actuator": "motor"},
    )
    assert response.status_code == 422


def test_direct_request_rejects_legacy_actuator_shape(client):
    response = client.post("/api/v1/control/direct", json={"actuator": "motor"})
    assert response.status_code == 422
