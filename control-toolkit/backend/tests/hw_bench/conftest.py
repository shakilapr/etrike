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
import time

import pytest

from harness import (
    CAN_RT_STATE_RPT,
    ESTOP_REASON_WATCHDOG,
    HIGH,
    LOW,
    OBSTACLE_CLEAR,
    HwBench,
    signal_of,
)

# How long after entering AUTO to wait for the MTR-absent watchdog signature.
_BYPASS_GUARD_S = 1.5


def _fail_if_bench_bypass_inactive(client: HwBench) -> None:
    """Fail fast (once) when the GPIO42 developer-override jumper is missing.

    With ``SYSTEM_RUN_MODE=1`` the bench bypasses require GPIO42 jumpered to
    GND. Without it ``g_bypass_mtr_absent`` is false, so the absent MTR trips
    ``RtMtrFbkTimeout`` (``estop_reason=10``) a few hundred ms after AUTO
    (``kMtrFbkAcquireGraceMs``) and every drive setpoint is zeroed — which
    otherwise surfaces as ~10 opaque assertion failures across the suite.
    """
    client.command_power(True)
    ok, _ = client.command_mode(True)
    if not ok:
        return  # not the failure signature we guard for; let tests report it
    deadline = time.monotonic() + _BYPASS_GUARD_S
    while time.monotonic() < deadline:
        reason = signal_of(client.state_map().get((HIGH, CAN_RT_STATE_RPT)), "estop_reason")
        if reason == ESTOP_REASON_WATCHDOG:
            pytest.fail(
                "GPIO42 developer-override jumper is NOT installed: bench bypasses "
                "are inactive and the absent MTR trips RtMtrFbkTimeout "
                "(estop_reason=10), which zeroes all motion. Jumper GPIO42 to GND, "
                "reboot RT, then re-run the suite."
            )
        time.sleep(0.1)


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

    # SYS boots into ESTOP and needs one staged reset before it will accept
    # power/AUTO; normalise that here so every test starts from STANDBY.
    client.ensure_operational()

    # One-time rig guard: abort with a clear message if the bench bypasses are
    # inactive (GPIO42 not jumped), then return to a parked MANUAL state.
    _fail_if_bench_bypass_inactive(client)
    client.park()

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
    bench.ensure_session()
    bench.stop_all()
    yield
    bench.park()


@pytest.fixture
def auto_ready(bench: HwBench) -> HwBench:
    """Vehicle powered ON and in AUTO (drive-ready), per handoff Step 1."""
    bench.ensure_session()
    bench.ensure_operational()
    ok_power, _ = bench.command_power(True)
    assert ok_power, "SYS_PWR_CMD never reached power_state=1 (power ON)"

    # Normalise latched Host inputs: RT holds the last commanded obstacle/brake
    # indefinitely, so a test that failed before releasing them would otherwise
    # zero every later drive setpoint (0x205 = obstacle kpa, 0x204 = 0). Repeat
    # the clear so a single dropped/echoed frame cannot leave it latched.
    for _ in range(4):
        bench.send_obstacle(OBSTACLE_CLEAR)
        bench.send_brake(0)
        time.sleep(0.05)

    ok_mode, state = bench.command_mode(True)
    assert ok_mode, (
        "AUTO transition failed: SYS_MODE_CMD/RT_STATE_RPT never reported mode=1 "
        f"(SYS_MODE_CMD={state.get((LOW, 0x110))})"
    )
    return bench
