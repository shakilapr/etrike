from __future__ import annotations

import pytest
from fastapi.testclient import TestClient

from vtc.config import VtcConfig
from vtc.main import create_app


@pytest.fixture()
def client() -> TestClient:
    app = create_app(VtcConfig())
    with TestClient(app) as c:
        yield c
