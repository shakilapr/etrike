"""pytest fixtures for the L9 system bench.

Strategy:
  * one session-scoped configuration resolved from CLI/env;
  * function-scoped ``bench`` = a full, running BenchSession on the best
    available link (physical CANalyst-II when reachable, else loopback);
  * ``physical_bench`` = the same session but *skips* when no real bench is
    connected — the gate used by every real-ECU scenario;
  * ``vb`` = a fresh loopback-only session for harness self-tests, so they stay
    deterministic even when a physical bench is present.
"""
from __future__ import annotations

import time
from pathlib import Path

import pytest

from bench.config import BenchConfig
from bench.session import create_session

REPO = Path(__file__).resolve().parent


def pytest_addoption(parser):
    parser.addoption("--bench", action="store", default="auto",
                     help="bench transport: auto | canalystii | loopback")
    parser.addoption("--traces", action="store", default=None,
                     help="override traces root directory")


@pytest.fixture(scope="session")
def cfg(request) -> BenchConfig:
    transport = request.config.getoption("--bench")
    traces_root = Path(request.config.getoption("--traces")) if request.config.getoption("--traces") else REPO / "traces"
    return BenchConfig(transport=transport, traces_root=traces_root)


def _start(session) -> None:
    session.capture.start()
    session.restbus.start()


def _stop(session, traces_root: Path) -> None:
    session.restbus.stop()
    session.capture.stop()
    session.link.close()
    _write_trace(session, traces_root)


def _write_trace(session, traces_root: Path) -> None:
    samples = session.capture.all()
    if not samples:
        return
    run_dir = traces_root / time.strftime("run_%Y%m%d_%H%M%S")
    run_dir.mkdir(parents=True, exist_ok=True)
    safe = "".join(ch if ch.isalnum() or ch in "._-" else "_" for ch in session.description)
    path = run_dir / f"{safe}_frames.jsonl"
    with path.open("w", encoding="utf-8") as handle:
        for sample in samples:
            handle.write(str(sample.to_trace()) + "\n")


@pytest.fixture
def bench(cfg):
    """A running BenchSession on the best available link."""
    session = create_session(cfg, traces_path=cfg.traces_root)
    _start(session)
    try:
        yield session
    finally:
        _stop(session, cfg.traces_root)


@pytest.fixture
def physical_bench(bench):
    """The same bench, but every consumer is skipped without real hardware."""
    if not bench.is_physical:
        pytest.skip(f"no physical bench connected ({bench.description}); attach CANalyst-II + ECUs to run")
    return bench


@pytest.fixture
def fault_capable_bench(bench):
    """A physical bench that can also *drop/corrupt* real-node frames.

    On a passive single CANalyst-II you can observe and transmit but not delete
    frames that a real ECU owns. Dropping SYS 0x011 / RT 0x7FD / MTR 0x206 etc.
    needs a bus-mastering second adapter or a gateway (SYSTEMTEST_FAULT_ADAPTER=1).
    """
    if not bench.is_physical:
        pytest.skip(f"no physical bench connected ({bench.description}); attach CANalyst-II + ECUs to run")
    if not __import__("os").environ.get("SYSTEMTEST_FAULT_ADAPTER"):
        pytest.skip("real-node frame faults need a bus-mastering fault adapter (SYSTEMTEST_FAULT_ADAPTER=1)")
    return bench


@pytest.fixture
def vb(cfg):
    """Loopback-only session for deterministic harness self-tests.

    Only the capture worker runs; restbus nodes are started explicitly by each
    test so counts and timelines are deterministic.
    """
    loopback_cfg = BenchConfig(transport="loopback", traces_root=cfg.traces_root)
    session = create_session(loopback_cfg, traces_path=cfg.traces_root)
    session.capture.start()
    try:
        yield session
    finally:
        session.restbus.stop()
        session.capture.stop()
        session.link.close()
