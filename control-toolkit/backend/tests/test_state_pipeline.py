"""End-to-end virtual pipeline through the API (workplan §1 exit gate).

Inject a frame on the virtual bus and verify it appears decoded in
``GET /api/v1/state`` — exercising transport -> router task -> latest store ->
API, all in the Pure Software profile with no hardware.
"""

from __future__ import annotations

import time

from control_toolkit.models.frames import ChannelId


def test_injected_frame_appears_decoded_in_state_api(client):
    transport = client.app.state.lifecycle.transport
    assert transport is not None  # Pure Software opens a virtual transport

    transport.inject(ChannelId.LOW, 0x7FE, bytes.fromhex("ffff"))

    body = {"messages": []}
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        body = client.get("/api/v1/state").json()
        if body["messages"]:
            break
        time.sleep(0.02)

    msgs = {m["name"]: m for m in body["messages"]}
    assert "SYS_HEARTBEAT" in msgs
    hb = msgs["SYS_HEARTBEAT"]
    assert hb["freshness"] == "live"
    assert hb["validation_status"] == "ok"
    assert hb["signals"]["alive_ctr"]["engineering_value"] == 255


def test_status_reports_open_virtual_adapter(client):
    body = client.get("/api/v1/status").json()
    assert body["adapter"]["identity"] == "virtual"
    assert body["adapter"]["capability"]["hw_timestamps"] is False
    assert body["adapter"]["capability"]["tec_rec_reporting"] is False
