#!/usr/bin/env python3
"""
Generate all E-Trike DBC files from shared/can/can_signals.yaml.

Single source of truth: YAML -> pydantic -> canmatrix -> .dbc files.

Usage:
  python generate_all_dbc.py                    # generate all 3 DBCs
  python generate_all_dbc.py --check            # generate + re-parse + smoke test
  python generate_all_dbc.py --check --smoke    # generate + re-parse + full smoke test
  python generate_all_dbc.py --protocol custom  # single protocol only
  python generate_all_dbc.py --summary          # print signal table to stdout
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
    load_can_database, dump_signal_summary,
    CanDatabase, ProtocolDef, MessageDef, SignalDef, ByteOrder,
)

REPO_ROOT = Path(__file__).resolve().parent.parent.parent  # etrike/
YAML_PATH = Path(__file__).resolve().parent / "can_signals.yaml"


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
    buf = BytesIO()
    canmatrix.formats.dbc.dump(db, buf)
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "wb") as f:
        f.write(buf.getvalue())
    return len(buf.getvalue())


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
        print("Run from the repo root or create shared/can/can_signals.yaml", file=sys.stderr)
        sys.exit(1)

    db: CanDatabase = load_can_database(YAML_PATH)
    print(f"Loaded {YAML_PATH}: {len(db.protocols)} protocol(s), {len(db.ecus)} ECU(s)")

    if "--summary" in sys.argv:
        print(dump_signal_summary(db))
        return

    do_check = "--check" in sys.argv
    do_smoke = "--smoke" in sys.argv

    # Filter protocols (--protocol custom|syntree_eps|syntree_seb)
    protocol_names = list(db.protocols.keys())
    for i, arg in enumerate(sys.argv):
        if arg == "--protocol" and i + 1 < len(sys.argv):
            protocol_names = [sys.argv[i + 1]]
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
