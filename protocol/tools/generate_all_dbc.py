#!/usr/bin/env python3
"""
Generate all E-Trike DBC files from shared/can/ YAML directory.

Single source of truth: shared/can/can_shared.yaml + can_high.yaml + can_low.yaml.
YAML directory -> pydantic -> canmatrix -> .dbc files.

Usage:
  python generate_all_dbc.py                        # generate all 4 DBCs
  python generate_all_dbc.py --check                # generate + re-parse + smoke test
  python generate_all_dbc.py --check --smoke        # generate + re-parse + full smoke test
  python generate_all_dbc.py --protocol custom_high # single protocol only
  python generate_all_dbc.py --summary              # print signal table to stdout
"""

import os
import sys
from io import BytesIO
from pathlib import Path
from typing import Optional

import canmatrix
import canmatrix.formats.dbc
from canmatrix import CanMatrix, Ecu, Frame, Signal, ArbitrationId

from can_signals_schema import (
    load_can_database, load_can_database_dir, dump_signal_summary,
    CanDatabase, ProtocolDef, MessageDef, SignalDef, ByteOrder,
)

REPO_ROOT = Path(__file__).resolve().parent.parent.parent  # etrike/
YAML_PATH = Path(__file__).resolve().parent  # shared/can/ directory (was single file)


# ── Conversion: YAML -> canmatrix ─────────────────────────────────────

def signal_to_canmatrix(sig: SignalDef, byte_order: ByteOrder) -> Signal:
    """Convert a validated SignalDef to a canmatrix Signal."""
    start_bit = sig.compute_start_bit(byte_order)
    is_le = (byte_order == ByteOrder.intel)
    s = Signal(
        sig.name,
        start_bit=start_bit,
        size=sig.size,
        is_little_endian=is_le,
        is_signed=(sig.type == "signed"),
        factor=sig.factor,
        offset=sig.offset,
        min=sig.min,
        max=sig.max,
        unit=sig.unit,
        receivers=list(sig.receivers),
        comment=sig.comment,
    )
    if sig.values:
        s.values = dict(sig.values)
    return s


def message_to_canmatrix(msg: MessageDef, byte_order: ByteOrder) -> Frame:
    """Convert a validated MessageDef to a canmatrix Frame."""
    f = Frame(
        msg.name,
        arbitration_id=ArbitrationId(msg.id),
        size=msg.dlc,
        transmitters=[msg.sender],
        cycle_time=msg.cycle_ms,
        comment=msg.comment,
    )
    for sig in msg.signals:
        f.add_signal(signal_to_canmatrix(sig, byte_order))
    return f


def build_database(proto: ProtocolDef, ecu_defs) -> CanMatrix:
    """Build a CanMatrix for one protocol definition."""
    db = CanMatrix()

    # Collect referenced ECUs
    proto_ecu_names: set[str] = set()
    for msg in proto.messages:
        proto_ecu_names.add(msg.sender)
        for sig in msg.signals:
            proto_ecu_names.update(sig.receivers)

    for ecu in ecu_defs:
        if ecu.name in proto_ecu_names:
            db.add_ecu(Ecu(ecu.name, ecu.comment))

    for msg in proto.messages:
        db.add_frame(message_to_canmatrix(msg, proto.byte_order))

    return db


def write_dbc(db: CanMatrix, output_path: str | Path) -> int:
    """Write a CanMatrix to a .dbc file. Returns byte count."""
    import re
    buf = BytesIO()
    canmatrix.formats.dbc.dump(db, buf)
    text = buf.getvalue().decode("utf-8")
    # Round excessive IEEE 754 float precision (canmatrix artifact)
    def round_float(m):
        val = float(m.group(0))
        if abs(val) < 1e-10:
            return "0"
        # 6 significant digits is more than enough for CAN scaling
        return f"{val:.6g}"
    text = re.sub(r'(?<!\d)(?:\d+\.\d{15,}|0\.\d{10,})(?!\d)', round_float, text)
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(text)
    return len(text)


def validate_dbc(dbc_path: str | Path) -> int:
    """Re-parse a .dbc file with canmatrix. Returns frame count."""
    with open(dbc_path, "rb") as fh:
        db2 = canmatrix.formats.dbc.load(fh, dbcImportEncoding="utf-8")
    return len(db2.frames)


def smoke_test_frame(frame: Frame) -> Optional[str]:
    """Encode/decode roundtrip a frame. Returns error message or None."""
    try:
        sample = {}
        for sig in frame.signals:
            # Use midpoint of range or zero
            mn = sig.min if sig.min is not None else 0
            mx = sig.max if sig.max is not None else 255
            sample[sig.name] = float((mn + mx) / 2)
        enc = frame.encode(sample)
        dec = frame.decode(enc)
        for sig in frame.signals:
            if sig.name not in dec:
                return f"signal '{sig.name}' missing from decode"
        return None
    except Exception as e:
        return str(e)


# ── Main ──────────────────────────────────────────────────────────────

def main():
    if not YAML_PATH.exists():
        print(f"Error: {YAML_PATH} not found", file=sys.stderr)
        print("Run from the repo root or ensure shared/can/ directory exists", file=sys.stderr)
        sys.exit(1)

    # Auto-detect: directory (new split format) or single file (legacy)
    if YAML_PATH.is_dir():
        db: CanDatabase = load_can_database_dir(YAML_PATH)
    else:
        db: CanDatabase = load_can_database(YAML_PATH)
    print(f"Loaded {YAML_PATH}: {len(db.protocols)} protocol(s), {len(db.ecus)} ECU(s)")

    if "--summary" in sys.argv:
        print(dump_signal_summary(db))
        return

    do_check = "--check" in sys.argv
    do_smoke = "--smoke" in sys.argv

    # Filter protocols (--protocol custom_high|custom_low|sbw_unit|bbw_unit)
    protocol_names = list(db.protocols.keys())
    for i, arg in enumerate(sys.argv):
        if arg == "--protocol" and i + 1 < len(sys.argv):
            requested = sys.argv[i + 1]
            # Legacy alias: --protocol custom → suggest new names
            if requested == "custom":
                print("Error: 'custom' protocol has been split into 'custom_high' and 'custom_low'.", file=sys.stderr)
                print("Use --protocol custom_high or --protocol custom_low instead.", file=sys.stderr)
                sys.exit(1)
            protocol_names = [requested]
            break

    total_bytes = 0
    total_frames = 0

    for pname in protocol_names:
        if pname not in db.protocols:
            print(f"Error: unknown protocol '{pname}'. "
                  f"Available: {list(db.protocols.keys())}", file=sys.stderr)
            sys.exit(1)

        proto = db.protocols[pname]
        output_path = REPO_ROOT / proto.output

        can_db = build_database(proto, db.ecus)
        nbytes = write_dbc(can_db, output_path)
        total_bytes += nbytes

        print(f"  [{pname:15s}] -> {proto.output} "
              f"({nbytes} bytes, {len(can_db.frames)} frames, {len(can_db.ecus)} ECUs)")

        if do_check:
            nframes = validate_dbc(output_path)
            total_frames += nframes
            print(f"    Validated: {nframes} frames re-parsed OK")

            if do_smoke:
                errors = 0
                for frame in can_db.frames:
                    if frame.signals:
                        err = smoke_test_frame(frame)
                        if err:
                            print(f"    Smoke FAIL: {frame.name} — {err}")
                            errors += 1
                        else:
                            print(f"    Smoke OK:   {frame.name}")
                if errors:
                    print(f"    {errors} smoke test(s) FAILED")
                else:
                    print(f"    All {len(can_db.frames)} frames smoke-tested OK")

    print(f"\nDone: {len(protocol_names)} DBC(s), {total_bytes} bytes total")


if __name__ == "__main__":
    main()
