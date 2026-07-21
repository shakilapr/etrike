"""Real mode without CANalyst must not show Computer-mode live CAN ghosts."""

from __future__ import annotations

import time

from fastapi.testclient import TestClient

from control_toolkit.config import ToolkitConfig
from control_toolkit.main import create_app


def test_switching_to_real_without_adapter_clears_live_messages() -> None:
    app = create_app(ToolkitConfig())
    with TestClient(app) as client:
        # Computer session + traffic
        created = client.post("/api/v1/sessions", json={"profile": "pure_software"})
        assert created.status_code == 200
        ses = created.json()["session"]
        client.post(
            f"/api/v1/sessions/{ses['session_id']}/bench-tx",
            json={"enabled": True, "expected_revision": ses["revision"]},
        )
        client.post("/api/v1/simulation/start")
        time.sleep(0.4)
        before = client.get("/api/v1/state").json()["messages"]
        assert len(before) > 0, "expected SYS/virtual traffic before switch"

        # Switch to Real Bench without CANalyst
        st = client.get("/api/v1/status").json()["session"]
        changed = client.post(
            f"/api/v1/sessions/{st['session_id']}/profile",
            json={
                "profile": "bench_test",
                "expected_revision": st["revision"],
                "confirm": True,
            },
        )
        assert changed.status_code == 200, changed.text

        after_status = client.get("/api/v1/status").json()
        assert after_status["session"]["profile"] == "bench_test"
        assert after_status["session"]["destination"] == "physical"
        # No adapter identity when missing
        assert after_status["adapter"]["identity"] in ("none", "absent", None) or (
            after_status["adapter"].get("health") in ("absent", "closed")
        )

        after = client.get("/api/v1/state").json()["messages"]
        assert after == [] or len(after) == 0, (
            f"ghost messages after Real-without-adapter: "
            f"{[(m.get('bus'), m.get('name'), m.get('freshness')) for m in after]}"
        )

        sim = client.get("/api/v1/simulation").json()["simulation"]
        assert sim["virtual_can"]["state"] == "stopped"
        assert sim["sys_sil"]["state"] in ("stopped", "unavailable")

        # Adapter must not report virtual channel activity leftovers
        ch = after_status.get("adapter", {}).get("channels") or {}
        assert ch == {} or all(
            (c.get("activity") in (None, "unseen", "quiet")) for c in ch.values()
        )


def test_status_absent_adapter_has_empty_channels() -> None:
    app = create_app(ToolkitConfig())
    with TestClient(app) as client:
        created = client.post("/api/v1/sessions", json={"profile": "pure_software"})
        ses = created.json()["session"]
        client.post(
            f"/api/v1/sessions/{ses['session_id']}/profile",
            json={
                "profile": "bench_test",
                "expected_revision": ses["revision"],
                "confirm": True,
            },
        )
        st = client.get("/api/v1/status").json()
        assert st["adapter"]["identity"] == "none"
        assert st["adapter"]["health"] == "absent"
        assert st["adapter"].get("channels") in ({}, None) or len(st["adapter"]["channels"]) == 0
