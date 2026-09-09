#!/usr/bin/env python3
"""Generate PlatformIO compilation databases for all firmware targets.

Targets:
  - rt-esp32
  - sys-esp32
  - mtr-stm32
  - pwt-esp32
  - rm-esp32

Usage:
  python scripts/generate_compile_commands.py
  python scripts/generate_compile_commands.py --target rt-esp32
"""

import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

TARGETS = [
    "rt-esp32",
    "sys-esp32",
    "mtr-stm32",
    "pwt-esp32",
    "rm-esp32",
]

def generate_db(target: str, pio_cmd: str) -> bool:
    target_dir = ROOT / target
    if not (target_dir / "platformio.ini").is_file():
        print(f"Skipping {target}: platformio.ini not found")
        return False

    print(f"Building compilation database for {target}...", flush=True)
    t0 = time.time()
    res = subprocess.run(
        [pio_cmd, "run", "-d", str(target_dir), "-t", "compiledb"],
        cwd=str(ROOT),
        capture_output=True,
        text=True,
    )
    elapsed = time.time() - t0

    db_path = target_dir / "compile_commands.json"
    if res.returncode == 0 and db_path.is_file():
        size_kb = db_path.stat().st_size / 1024
        print(f"  [OK] {target}/compile_commands.json generated ({size_kb:.1f} KB, {elapsed:.1f}s)")
        return True
    else:
        print(f"  [FAIL] {target} failed (code {res.returncode}):")
        if res.stderr:
            print(res.stderr.strip())
        elif res.stdout:
            print(res.stdout.strip())
        return False

def main():
    parser = argparse.ArgumentParser(description="Generate compilation databases for etrike firmware targets")
    parser.add_argument(
        "--target",
        "-t",
        choices=TARGETS,
        help="Generate only for a specific target",
    )
    args = parser.parse_args()

    pio_cmd = shutil.which("pio.cmd") or shutil.which("pio")
    if not pio_cmd:
        print("ERROR: PlatformIO (pio) not found on PATH.", file=sys.stderr)
        sys.exit(1)

    targets = [args.target] if args.target else TARGETS
    print(f"Generating compilation databases for {len(targets)} targets...\n")

    success_count = 0
    for target in targets:
        if generate_db(target, pio_cmd):
            success_count += 1

    print(f"\nCompleted: {success_count}/{len(targets)} targets succeeded.")
    if success_count == len(targets):
        print("VS Code IntelliSense is configured with the latest translation units.")
    else:
        sys.exit(1)

if __name__ == "__main__":
    main()
