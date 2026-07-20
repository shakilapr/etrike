from __future__ import annotations

from pathlib import Path

from fastapi.testclient import TestClient

from control_toolkit.config import ToolkitConfig
from control_toolkit.main import create_app


def test_simulation_status_is_truthful_without_configured_sil(client) -> None:
    response = client.get("/api/v1/simulation")
    assert response.status_code == 200
    status = response.json()["simulation"]
    assert status["mode"] == "computer"
    assert status["virtual_can"]["state"] == "running"
    assert status["rt_sil"]["configured"] is False
    assert status["rt_sil"]["state"] == "stopped"
    assert status["sys_sil"]["state"] == "unavailable"

    start = client.post("/api/v1/simulation/start")
    assert start.status_code == 503
    assert start.json()["code"] == "simulation.not_configured"

    stop = client.post("/api/v1/simulation/stop")
    assert stop.status_code == 200
    assert stop.json()["simulation"]["virtual_can"]["state"] == "running"


def test_native_rt_sil_can_stop_and_restart_without_closing_virtual_can() -> None:
    executable = (
        Path(__file__).parents[3]
        / "native-test"
        / "build-sil"
        / "sim_engine_native.exe"
    )
    assert executable.is_file()
    app = create_app(ToolkitConfig(native_sil_executable=str(executable)))
    with TestClient(app) as client:
        initial = client.get("/api/v1/simulation").json()["simulation"]
        assert initial["rt_sil"]["state"] == "running"
        assert initial["rt_sil"]["pid"] is not None

        stopped = client.post("/api/v1/simulation/stop").json()["simulation"]
        assert stopped["rt_sil"]["state"] == "stopped"
        assert stopped["virtual_can"]["state"] == "running"

        restarted = client.post("/api/v1/simulation/start").json()["simulation"]
        assert restarted["rt_sil"]["state"] == "running"
        assert restarted["virtual_can"]["state"] == "running"

        again = client.post("/api/v1/simulation/start").json()["simulation"]
        assert again["rt_sil"]["state"] == "running"
