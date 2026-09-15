"""Fixtures for the live hardware-bench suite.

The suite runs against a *running* control-toolkit backend that owns the
CANalyst-II adapter. If the backend is unreachable or reports no physical link,
every hardware test skips cleanly (so ``pytest`` in CI stays green).

Bring-up:

    # terminal 1 — the backend owns the USB CAN adapter
    cd control-toolkit/backend
    python -m uvicorn control_toolkit.main:app --host 127.0.0.1 --port 8001

    # terminal 2 — run the bench suite
    cd control-toolkit/backend
    pytest tests/hw_bench -v

Override the backend URL with ``--api-url`` or ``CTK_API_URL``.
"""

from __future__ import annotations

import os

import pytest

from harness import LOW, HwBench


def pytest_addoption(parser) -> None:
    parser.addoption(
        "--api-url",
        action="store",
        default=None,
        help="control-toolkit backend base URL (default: env CTK_API_URL or http://127.0.0.1:8001)",
    )
    parser.addoption(
        "--hw-timeout",
        action="store",
        type=float,
        default=3.0,
        help="default wait timeout (s) for hardware-bench assertions",
    )


@pytest.fixture(scope="session")
def api_url(request) -> str:
    return (
        request.config.getoption("--api-url")
        or os.environ.get("CTK_API_URL")
        or "http://127.0.0.1:8001"
    )


@pytest.fixture(scope="session")
def bench(request, api_url) -> HwBench:
    """A Bench TX-enabled ``bench_test`` session on the physical CANalyst-II."""
    client = HwBench(base_url=api_url)
    client.timeout_s = request.config.getoption("--hw-timeout")

    try:
        status = client.get_status()
    except Exception as exc:  # noqa: BLE001
        pytest.skip(f"control-toolkit backend unreachable at {api_url}: {exc}")
    if not status or not status.get("ready"):
        pytest.skip(f"control-toolkit backend not ready at {api_url}")

    link = status.get("link") or {}
    if not link.get("connected"):
        adapter = (status.get("adapter") or {})
        pytest.skip(
            "physical CANalyst-II link not connected "
            f"(adapter health={adapter.get('health')!r}, detail={adapter.get('last_error')!r}); "
            "attach the bench and start the backend with the adapter free"
        )

    try:
        client.setup_session(profile="bench_test")
    except Exception as exc:  # noqa: BLE001
        pytest.skip(f"could not establish bench_test session: {exc}")

    yield client


@pytest.fixture(autouse=True)
def _bench_isolation(request):
    """Guarantee a clean starting point and a safe idle after every bench test.

    Only engages for tests that actually request the ``bench`` fixture, so
    hardware-free guard/self-tests still run when no bench is attached.
    """
    if "bench" not in request.fixturenames:
        yield
        return
    bench = request.getfixturevalue("bench")
    bench.stop_all()
    yield
    bench.park()


@pytest.fixture
def auto_ready(bench: HwBench) -> HwBench:
    """Vehicle powered ON and in AUTO (drive-ready), per handoff Step 1."""
    ok_power, _ = bench.command_power(True)
    assert ok_power, "SYS_PWR_CMD never reached power_state=1 (power ON)"
    ok_mode, state = bench.command_mode(True)
    assert ok_mode, (
        "AUTO transition failed: SYS_MODE_CMD/RT_STATE_RPT never reported mode=1 "
        f"(SYS_MODE_CMD={state.get((LOW, 0x110))})"
    )
    return bench
