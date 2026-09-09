"""L9 system bench package.

``create_session`` from ``bench.session`` is the single entry point used by the
pytest fixtures: it opens the best available CAN link (physical CANalyst-II when
present, loopback otherwise), starts the frame capture worker and assembles the
restbus rig, GPIO and power boards.

Typical usage (hardware):
    python -m pytest scenarios -m hardware --bench=canalystii

Typical usage (harness self-test, no hardware):
    python -m pytest scenarios -m selftest
"""
from .can import CanLink, CanalystLink, VirtualLink
from .capture import Capture
from .config import BenchConfig
from .session import BenchSession, create_session
from .wire import Sample

__all__ = [
    "BenchConfig",
    "BenchSession",
    "CanLink",
    "CanalystLink",
    "VirtualLink",
    "Capture",
    "Sample",
    "create_session",
]
