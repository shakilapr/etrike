"""Managed SYS SIL peer emits protocol frames on virtual CAN."""

from __future__ import annotations

import time

from fastapi.testclient import TestClient

from control_toolkit.config import ToolkitConfig
from control_toolkit.main import create_app


def test_sys_sil_emits_heartbeat_and_safety_on_virtual_can() -> None:
    app = create_app(ToolkitConfig())
    with TestClient(app) as client:
        sim = client.get("/api/v1/simulation").json()["simulation"]
        assert sim["sys_sil"]["state"] == "running"
        assert sim["sys_sil"]["available"] is True

        # Allow a few heartbeat periods (100 ms).
        deadline = time.time() + 2.0
        names: set[str] = set()
        while time.time() < deadline:
            state = client.get("/api/v1/state").json()
            for m in state.get("messages") or []:
                if m.get("name"):
                    names.add(str(m["name"]))
            if "SYS_HEARTBEAT" in names and "SYS_SAFETY_STS" in names:
                break
            time.sleep(0.05)

        assert "SYS_HEARTBEAT" in names, f"expected SYS_HEARTBEAT in {sorted(names)}"
        assert "SYS_SAFETY_STS" in names, f"expected SYS_SAFETY_STS in {sorted(names)}"

        stopped = client.post("/api/v1/simulation/stop").json()["simulation"]
        assert stopped["sys_sil"]["state"] == "stopped"

        started = client.post("/api/v1/simulation/start").json()["simulation"]
        assert started["sys_sil"]["state"] == "running"
