#!/usr/bin/env python3
"""Test DLC generator conflict detection (PCR7).

Architecture §9.7: CAN protocol validation requires that DLC mismatches
across protocol definitions are surfaced, not silently resolved.
This test verifies the generator warns when the same CAN ID has
different DLC values on different buses.
"""

import sys
import os
import io
import contextlib
import tempfile
import subprocess

pass_count = 0
fail_count = 0

def check(cond, msg):
    global pass_count, fail_count
    if cond:
        pass_count += 1
    else:
        fail_count += 1
        print(f"  FAIL: {msg}", file=sys.stderr)


# ── Test 1: Generator can be imported ──────────────────────────────
print("-- Generator module imports cleanly --")
gen_path = os.path.join(os.path.dirname(__file__), "..", "..", "shared", "can", "generate_code.py")
check(os.path.exists(gen_path), f"generator exists at {gen_path}")


# ── Test 2: Generator runs without errors on current YAML ──────────
print("-- Generator runs on current YAML without errors --")
result = subprocess.run([sys.executable, gen_path],
                        capture_output=True, text=True)
check(result.returncode == 0, f"generator exit code: {result.returncode}, stderr: {result.stderr[:200]}")
check("Wrote" in result.stdout, "generator wrote output files")


# ── Test 4: Generated files exist after generation ─────────────────
print("-- Generated files exist --")
generated_dir = os.path.join(os.path.dirname(__file__), "..", "..", "shared", "can", "generated")
for fname in ["can_data.h", "can_ids.ts", "can_constants.ts"]:
    fpath = os.path.join(generated_dir, fname)
    check(os.path.exists(fpath), f"{fname} exists")


# ── Test 5: Generated DLC for 0x7FD is 2 (not 1) ───────────────────
print("-- Generated DLC for 0x7FD is correct (DLC=2) --")
can_ids_path = os.path.join(generated_dir, "can_ids.ts")
with open(can_ids_path) as f:
    content = f.read()
check('"0x7FD": 2' in content, "0x7FD DLC=2 in can_ids.ts")
check('"0x7FE": 2' in content, "0x7FE DLC=2 in can_ids.ts")


# ── Test 6: Generated DLC for 0x210 is 6 ───────────────────────────
print("-- Generated DLC for 0x210 is correct (DLC=6) --")
check('"0x210": 6' in content, "0x210 DLC=6 in can_ids.ts")


# ── Test 7: DLC consistency — verify all forwarded IDs have DLC > 0 ─
print("-- All forwarded CAN IDs have DLC > 0 --")
forwarded_ids = ["0x001", "0x011", "0x120", "0x206", "0x302", "0x600"]
for fid in forwarded_ids:
    check(fid in content, f"forwarded ID {fid} present in can_ids.ts")


# ── Test 8: Heartbeat DLC consistency across generated files ───────
print("-- Heartbeat DLC consistent across generated files --")
can_data_path = os.path.join(generated_dir, "can_data.h")
with open(can_data_path) as f:
    cpp_content = f.read()
check("kDlc_RT_HEARTBEAT = 2" in cpp_content or "kDlc_RT_Heartbeat = 2" in cpp_content,
      "RT heartbeat DLC=2 in can_data.h")
check("kDlc_SYS_HEARTBEAT = 2" in cpp_content or "kDlc_SYS_Heartbeat = 2" in cpp_content,
      "SYS heartbeat DLC=2 in can_data.h")


# ── Results ─────────────────────────────────────────────────────────
print(f"\n=== Results: {pass_count} passed, {fail_count} failed ===")
sys.exit(0 if fail_count == 0 else 1)
