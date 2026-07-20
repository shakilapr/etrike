"""Software-in-the-loop round trip from Toolkit control API into real RT physics.

This deliberately exercises the production generated CAN codec on both sides:
control intent -> TxGate -> actual 0x300 bytes -> native RT PhysicsModel ->
0x204 bytes -> Toolkit virtual CAN RX/decode.  It is a focused RT physics SIL
test, not a claim that the full FreeRTOS RT/SYS applications are simulated.
"""

from __future__ import annotations

import json
import subprocess
import time
from pathlib import Path

from fastapi.testclient import TestClient

from control_toolkit.config import ToolkitConfig
from control_toolkit.main import create_app
from control_toolkit.models.frames import ChannelId


def _enable_pure_software_tx(client) -> None:
    session = client.post("/api/v1/sessions", json={"profile": "pure_software"}).json()["session"]
    response = client.post(
        f"/api/v1/sessions/{session['session_id']}/bench-tx",
        json={"enabled": True, "expected_revision": session["revision"]},
    )
    assert response.status_code == 200


def test_keyboard_intent_round_trips_through_native_rt_physics_sil(client) -> None:
    exe = Path(__file__).parents[3] / "native-test" / "build-sil" / "sim_engine_native.exe"
    assert exe.is_file(), "build native-test/build-sil/sim_engine_native before running SIL tests"

    _enable_pure_software_tx(client)
    transport = client.app.state.lifecycle.transport
    sent = []
    original_send = transport.send

    def capture_send(frame):
        sent.append(frame)
        return original_send(frame)

    transport.send = capture_send
    try:
        response = client.post(
            "/api/v1/control/intent",
            json={
                "sequence": 1,
                "source": "keyboard",
                "mode": "kinematics",
                "throttle": 0.5,
                "steer": 0.25,
                "gear": 1,
            },
        )
        assert response.status_code == 200
        deadline = time.monotonic() + 2.0
        host = None
        while time.monotonic() < deadline:
            host = next((f for f in sent if f.channel is ChannelId.HIGH and f.can_id == 0x300), None)
            if host is not None:
                break
            time.sleep(0.01)
        assert host is not None
    finally:
        transport.send = original_send

    wire_input = "\n".join(
        (
            json.dumps({"type": "frame", "bus": "high", "id": "0x300", "data": list(host.data)}),
            json.dumps({"type": "tick", "dt_ms": 10}),
        )
    ) + "\n"
    completed = subprocess.run(
        [str(exe)], input=wire_input, text=True, capture_output=True, check=True, timeout=10
    )
    output = [json.loads(line) for line in completed.stdout.splitlines() if line]
    assert any(item.get("type") == "ack" and item.get("id") == "0x300" for item in output)
    state = next(item for item in output if item.get("type") == "state" and "physics" in item)
    assert state["physics"]["motor_speed_mmps"] == 1500
    assert state["physics"]["steer_valid"] is True
    drive = next(item for item in output if item.get("name") == "RT_DRIVE_CMD")
    assert drive["id"] == "0x204"
    assert drive["data"] == [0, 0, 5, 220, 0]

    transport.inject(ChannelId.LOW, 0x204, bytes(drive["data"]))
    deadline = time.monotonic() + 2.0
    received = None
    while time.monotonic() < deadline:
        messages = client.get("/api/v1/state").json()["messages"]
        received = next((m for m in messages if m.get("name") == "RT_DRIVE_CMD"), None)
        if received is not None:
            break
        time.sleep(0.01)
    assert received is not None
    assert int(received["signals"]["motor_speed_mmps"]["engineering_value"]) == 1500


def test_managed_native_sil_connects_control_api_to_virtual_can() -> None:
    exe = Path(__file__).parents[3] / "native-test" / "build-sil" / "sim_engine_native.exe"
    assert exe.is_file(), "build native-test/build-sil/sim_engine_native before running SIL tests"
    app = create_app(ToolkitConfig(native_sil_executable=str(exe)))

    with TestClient(app) as client:
        _enable_pure_software_tx(client)
        response = client.post(
            "/api/v1/control/intent",
            json={
                "sequence": 1,
                "source": "keyboard",
                "mode": "kinematics",
                "throttle": 0.4,
                "steer": 0.2,
                "gear": 1,
            },
        )
        assert response.status_code == 200
        assert app.state.lifecycle.native_sil.running is True

        deadline = time.monotonic() + 3.0
        received = None
        while time.monotonic() < deadline:
            messages = client.get("/api/v1/state").json()["messages"]
            received = next((m for m in messages if m.get("name") == "RT_DRIVE_CMD"), None)
            if received is not None:
                break
            time.sleep(0.01)
        assert received is not None
        assert int(received["signals"]["motor_speed_mmps"]["engineering_value"]) == 1200

    assert app.state.lifecycle.native_sil is None
