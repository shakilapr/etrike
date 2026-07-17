"""Passive CANalyst-II dependency, USB, and dual-channel preflight.

This command never transmits a CAN frame.  Run from control-toolkit/backend:

    python scripts/canalyst_preflight.py
    python scripts/canalyst_preflight.py --open --listen-seconds 3
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from importlib.metadata import PackageNotFoundError, version

from control_toolkit.transport.canalyst import CanalystTransportAdapter, discover_canalyst


def _package_version(name: str) -> str | None:
    try:
        return version(name)
    except PackageNotFoundError:
        return None


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Passive CANalyst-II preflight; this command never transmits"
    )
    parser.add_argument("--device", type=int, default=0, help="USB device index")
    parser.add_argument("--bitrate", type=int, default=500_000)
    parser.add_argument(
        "--open",
        action="store_true",
        help="open both channels and prove the receive worker can start",
    )
    parser.add_argument(
        "--listen-seconds",
        type=float,
        default=0.0,
        help="passively count frames after --open (default: 0)",
    )
    args = parser.parse_args()

    dependencies = {
        "python": sys.version.split()[0],
        "python-can": _package_version("python-can"),
        "canalystii": _package_version("canalystii"),
        "pyusb": _package_version("pyusb"),
    }
    print(json.dumps({"stage": "dependencies", **dependencies}, sort_keys=True))
    missing = [name for name in ("python-can", "canalystii", "pyusb") if not dependencies[name]]
    if missing:
        print(
            json.dumps(
                {
                    "stage": "failed",
                    "reason": f"missing packages: {', '.join(missing)}",
                    "fix": 'python -m pip install -e ".[dev]"',
                },
                sort_keys=True,
            )
        )
        return 2

    found = discover_canalyst(
        device_index=args.device,
        bitrate=args.bitrate,
        force=True,
    )
    print(json.dumps({"stage": "probe", **found.model_dump()}, sort_keys=True))
    if not found.available:
        return 3
    if not args.open:
        print(
            json.dumps(
                {
                    "stage": "ready",
                    "next": "rerun with --open while USB-only; do not attach CAN cables yet",
                },
                sort_keys=True,
            )
        )
        return 0

    adapter = CanalystTransportAdapter(
        device_index=args.device,
        bitrate=args.bitrate,
    )
    try:
        adapter.open()
        frames = []
        deadline = time.monotonic() + max(0.0, args.listen_seconds)
        while time.monotonic() < deadline:
            frames.extend(adapter.poll(timeout=0.1))
        status = adapter.status().model_dump(mode="json")
        print(
            json.dumps(
                {
                    "stage": "dual_channel_open",
                    "transmitted": 0,
                    "observed_frames": len(frames),
                    "status": status,
                },
                sort_keys=True,
            )
        )
    except Exception as exc:  # noqa: BLE001
        print(
            json.dumps(
                {"stage": "failed", "reason": str(exc), "transmitted": 0},
                sort_keys=True,
            )
        )
        return 4
    finally:
        adapter.close()

    print(
        json.dumps(
            {
                "stage": "ready_for_unpowered_can_wiring",
                "next": "close the app, power off the bench, then wire CH0=High and CH1=Low",
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
