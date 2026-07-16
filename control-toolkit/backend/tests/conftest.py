from __future__ import annotations

import pytest
from fastapi.testclient import TestClient

from control_toolkit.config import ToolkitConfig
from control_toolkit.main import create_app


@pytest.fixture()
def client() -> TestClient:
    app = create_app(ToolkitConfig())
    with TestClient(app) as c:
        yield c
