#!/usr/bin/env python3
"""
Compatibility test: docs/io-autoware.md vs can_high.yaml + can_low.yaml.

Validates that every CAN ID, signal, and field referenced in the
Autoware.Auto interface documentation exists in the split YAML files
with the correct bus assignment, DLC, byte layout, and scaling.

Usage:
  python test_io_autoware_compat.py          # run all checks
  python test_io_autoware_compat.py --verbose  # print per-check detail
"""

import sys
from pathlib import Path
from can_signals_schema import load_can_database_dir, SignalType

REPO = Path(__file__).resolve().parent.parent.parent
CAN_DIR = Path(__file__).resolve().parent

# ── Expected mappings from docs/io-autoware.md §3 ────────────────────
# Each entry: (can_id, bus, signal_name, expected_properties)
# bus: "high", "low", or "both" (forwarded, appears in both DBCs)

EXPECTED = [
    # §3.3 CAN Protocol Compatibility — Command encoding
    ("0x300", "high", "HOST_DRIVE_CMD", {
        "dlc": 8,
        "comment_contains": "Autoware.Auto",
        "signals": {
            "HOST_DriveSpeed":  {"byte": 0, "size": 32, "type": "signed",  "unit": "mm/s"},
            "HOST_YawRate":     {"byte": 4, "size": 24, "type": "signed",  "unit": "mrad/s"},
            "HOST_Gear":        {"byte": 7, "size": 8,  "type": "unsigned", "values": {0: "N", 1: "D", 2: "S", 3: "R"}},
        },
    }),
    ("0x301", "high", "HOST_BRAKE_REQ", {
        "dlc": 4,
        "signals": {
            "HOST_BrakePressure": {"byte": 0, "size": 32, "type": "signed", "unit": "kPa", "min": 0, "max": 20000},
        },
    }),
    ("0x302", "both", "HOST_LIGHT_CMD", {
        "dlc": 1,
        "comment_contains": "Forwarded",
        "signals": {
            "HOST_LeftTurn":   {"byte": 0, "bit_offset": 0, "size": 1},
            "HOST_RightTurn":  {"byte": 0, "bit_offset": 1, "size": 1},
            "HOST_BrakeLight": {"byte": 0, "bit_offset": 2, "size": 1},
            "HOST_Headlight":  {"byte": 0, "bit_offset": 3, "size": 1},
        },
    }),
    ("0x001", "both", "SAFETY_ESTOP", {
        "dlc": 0,
        "comment_contains": "ESTOP",
        "signals": {},
    }),

    # §3.3 — Feedback decoding
    ("0x120", "both", "SYS_THROTTLE_STS", {
        "dlc": 2,
        "signals": {
            "SYS_ThrottleSpeed": {"byte": 0, "size": 16, "type": "signed", "unit": "mm/s", "min": -500, "max": 3000},
        },
    }),
    ("0x206", "both", "MTR_MOTOR_FBK", {
        "dlc": 4,
        "comment_contains": "forwarded",
        "signals": {
            "MTR_ActualSpeed": {"byte": 0, "size": 16, "type": "signed", "unit": "mm/s"},
            "MTR_GearState":   {"byte": 2, "size": 8, "type": "unsigned", "values": {0: "N", 1: "D", 2: "S", 3: "R"}},
            "MTR_FaultFlags":  {"byte": 3, "size": 8, "type": "unsigned"},
        },
    }),
    ("0x210", "high", "RT_STATE_RPT", {
        "dlc": 4,
        "signals": {
            "RT_Mode":       {"byte": 0, "size": 8, "type": "unsigned", "values": {0: "Manual", 1: "Auto", 2: "ESTOP"}},
            "RT_SteerValid": {"byte": 1, "size": 8},
            "RT_Reversing":  {"byte": 2, "size": 8},
            "RT_RxOverflow": {"byte": 3, "size": 8},
        },
    }),
    ("0x011", "both", "SYS_SAFETY_STS", {
        "dlc": 3,
        "comment_contains": "light state",
        "signals": {
            "SYS_EstopActive": {"byte": 0, "size": 8},
            "SYS_HeartbeatOk": {"byte": 1, "size": 8},
            "SYS_LightState":  {"byte": 2, "bit_offset": 0, "size": 4,
                                "comment_contains": "left_turn"},
        },
    }),

    # Steering diagnostics (GAP-2: offset=-3000 verified)
    ("0x310", "high", "STEER_DIAG", {
        "dlc": 8,
        "signals": {
            "SteerDiag_Angle0_1deg":  {"byte": 0, "size": 16, "factor": 0.1, "offset": -3000, "unit": "deg"},
            "SteerDiag_Fault":        {"byte": 2, "size": 8},
            "SteerDiag_MotorCurrent": {"byte": 3, "size": 16, "unit": "A"},
            "SteerDiag_ECUTemp":      {"byte": 5, "size": 16, "unit": "degC"},
            "SteerDiag_Reserved":     {"byte": 7, "size": 8, "min": 0, "max": 0},
        },
    }),

    # Brake diagnostics
    ("0x311", "high", "BRAKE_DIAG", {
        "dlc": 8,
        "signals": {
            "BrakeDiag_PressureRaw":  {"byte": 0, "size": 16, "unit": "MPa"},
            "BrakeDiag_Fault":        {"byte": 2, "size": 8},
            "BrakeDiag_MotorCurrent": {"byte": 3, "size": 16, "unit": "A"},
            "BrakeDiag_ECUTemp":      {"byte": 5, "size": 16, "unit": "degC"},
            "BrakeDiag_Reserved":     {"byte": 7, "size": 8, "min": 0, "max": 0},
        },
    }),

    # Obstacle distance
    ("0x400", "high", "HOST_OBSTACLE_DIST", {
        "dlc": 4,
        "signals": {
            "HOST_ObstacleDistance": {"byte": 0, "size": 32, "unit": "mm", "max": 4294967295},
        },
    }),

    # Diagnostics report
    ("0x600", "both", "SYS_DIAG_RPT", {
        "dlc": 8,
        "signals": {
            "SYS_DiagMode":         {"byte": 0, "size": 8},
            "SYS_DiagBrakeEngaged": {"byte": 1, "size": 8},
            "SYS_DiagHeartbeatOk":  {"byte": 2, "size": 8},
            "SYS_DiagEstopActive":  {"byte": 3, "size": 8},
            "SYS_DiagFreeHeapKb":   {"byte": 4, "size": 16, "unit": "KB"},
            "SYS_DiagTec":          {"byte": 6, "size": 8},
            "SYS_DiagRec":          {"byte": 7, "size": 8},
        },
    }),

    # Heartbeats
    ("0x7FC", "high", "HOST_HEARTBEAT", {
        "dlc": 1,
        "comment_contains": "controlled stop",
        "signals": {
            "Host_AliveCtr": {"byte": 0, "size": 8},
        },
    }),
    ("0x7FD", "both", "RT_HEARTBEAT", {
        "dlc": 1,
        "signals": {
            "RT_AliveCtr": {"byte": 0, "size": 8},
        },
    }),
    ("0x7FE", "low", "SYS_HEARTBEAT", {
        "dlc": 1,
        "signals": {
            "SYS_AliveCtr": {"byte": 0, "size": 8},
        },
    }),

    # Low-bus actuator commands
    ("0x204", "low", "RT_DRIVE_CMD", {
        "dlc": 5,
        "signals": {
            "RT_MotorSpeed": {"byte": 0, "size": 32, "type": "signed", "unit": "mm/s", "min": -500, "max": 3000},
            "RT_Gear":       {"byte": 4, "size": 8, "type": "unsigned", "values": {0: "N", 1: "D", 2: "S", 3: "R"}},
        },
    }),
    ("0x205", "low", "RT_BRAKE_CMD", {
        "dlc": 4,
        "signals": {
            "RT_BrakePressure": {"byte": 0, "size": 32, "type": "signed", "unit": "kPa", "min": 0, "max": 20000},
        },
    }),

    # SYNTREE EPS-C (low bus only)
    ("0x169", "low", "VCU_SES_REQ", {
        "dlc": 8,
        "signals": {
            "SES_CtrlEnable":  {"byte": 0, "bit_offset": 1, "size": 1},
            "SES_TgtStrAngle": {"byte": 2, "size": 16, "type": "signed", "offset": -3000, "unit": "deg"},
            "SES_Checksum":    {"byte": 7, "size": 8},
        },
    }),
    ("0x201", "low", "SES_STATUS", {
        "dlc": 8,
        "signals": {
            "SES_ErrorStatus": {"byte": 0, "bit_offset": 6, "size": 2},
            "SES_StrAngle":    {"byte": 2, "size": 16, "offset": -3000, "unit": "deg"},
            "SES_ChecksumStatus": {"byte": 7, "size": 8},
        },
    }),

    # SYNTREE SEB (low bus only)
    ("0x7B9", "low", "VCU_SEB_REQ", {
        "dlc": 8,
        "signals": {
            "SEB_CtrlEnable": {"byte": 0, "bit_offset": 1, "size": 1},
            "SEB_StrokeReq":  {"byte": 2, "size": 16, "unit": "mm"},
            "SEB_Checksum":   {"byte": 7, "size": 8},
        },
    }),
]


def msg_key(bus, can_id):
    """Normalize CAN ID to uppercase hex string."""
    if isinstance(can_id, str):
        can_id = int(can_id, 16) if can_id.startswith("0x") else int(can_id)
    return (bus, f"0x{can_id:03X}")


def build_index(db):
    """Build lookup: (bus, id_str) -> (protocol_name, message)."""
    idx = {}
    for pname, proto in db.protocols.items():
        for msg in proto.messages:
            id_str = f"0x{msg.id:03X}"
            bus = proto.bus
            key = (bus, id_str)
            if key in idx:
                # Forwarded frames: same ID on same bus from different protocols?
                # Only store first; cross-validation is separate
                pass
            idx[key] = (pname, msg)
            # Also index "both" as wildcard for forwarded frames
    return idx


def check_signal(msg, sig_name, expected):
    """Check a single signal against expectations."""
    errors = []
    sig = next((s for s in msg.signals if s.name == sig_name), None)
    if sig is None:
        errors.append(f"  MISSING signal '{sig_name}' in {msg.name}")
        return errors

    for attr, exp_val in expected.items():
        if attr == "comment_contains":
            if sig.comment is None or exp_val not in sig.comment:
                errors.append(
                    f"  {sig_name}.{attr}: expected to contain '{exp_val}', "
                    f"got '{sig.comment}'"
                )
        elif attr == "values":
            actual = dict(sig.values) if sig.values else {}
            if actual != exp_val:
                errors.append(
                    f"  {sig_name}.values: expected {exp_val}, got {actual}"
                )
        elif attr == "type":
            exp_type = SignalType(exp_val)
            if sig.type != exp_type:
                errors.append(
                    f"  {sig_name}.type: expected {exp_val}, got {sig.type.value}"
                )
        else:
            actual = getattr(sig, attr, None)
            if actual != exp_val:
                errors.append(
                    f"  {sig_name}.{attr}: expected {exp_val}, got {actual}"
                )
    return errors


def main():
    verbose = "--verbose" in sys.argv

    # Force UTF-8 on Windows to avoid cp1252 encoding errors from
    # unicode characters (arrows, etc.) in YAML comments.
    if sys.platform == "win32":
        import io
        sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

    db = load_can_database_dir(CAN_DIR)
    index = build_index(db)

    total_checks = 0
    total_errors = 0

    print("=" * 70)
    print("  E-Trike CAN <-> docs/io-autoware.md Compatibility Test")
    print("=" * 70)

    for entry in EXPECTED:
        can_id, bus, msg_name, props = entry
        id_str = f"0x{int(can_id, 16):03X}" if isinstance(can_id, str) else f"0x{can_id:03X}"

        # Find the message
        msg = None
        proto_name = None
        if bus == "both":
            # Must appear in both high-bus and low-bus DBCs
            high_key = ("high", id_str)
            low_key = ("low", id_str)
            h_msg = index.get(high_key)
            l_msg = index.get(low_key)
            if h_msg is None:
                print(f"FAIL: {id_str} {msg_name} — NOT FOUND on high bus")
                total_errors += 1
                continue
            if l_msg is None:
                print(f"FAIL: {id_str} {msg_name} — NOT FOUND on low bus")
                total_errors += 1
                continue
            proto_name, msg = h_msg  # use high-bus copy for signal checks
            if verbose:
                print(f"  {id_str} {msg_name}: high={h_msg[0]}, low={l_msg[0]}")
        else:
            key = (bus, id_str)
            entry = index.get(key)
            if entry is None:
                # Try other bus (might be misclassified as "both")
                alt_bus = "low" if bus == "high" else "high"
                alt_entry = index.get((alt_bus, id_str))
                if alt_entry:
                    print(f"WARN: {id_str} {msg_name} — expected on '{bus}' bus but found on '{alt_bus}'")
                    proto_name, msg = alt_entry
                else:
                    print(f"FAIL: {id_str} {msg_name} — NOT FOUND on '{bus}' bus")
                    total_errors += 1
                    continue
            else:
                proto_name, msg = entry

        total_checks += 1

        # Check message-level properties
        entry_errors = []
        if msg.name != msg_name:
            entry_errors.append(f"  NAME mismatch: expected {msg_name}, got {msg.name}")
        if msg.dlc != props.get("dlc"):
            entry_errors.append(f"  DLC mismatch: expected {props.get('dlc')}, got {msg.dlc}")
        if "comment_contains" in props:
            c = props["comment_contains"]
            if msg.comment is None or c.lower() not in msg.comment.lower():
                entry_errors.append(f"  COMMENT missing '{c}': {msg.comment}")

        # Check signals
        for sig_name, expected in props.get("signals", {}).items():
            entry_errors.extend(check_signal(msg, sig_name, expected))

        if entry_errors:
            print(f"\nFAIL: {id_str} {msg_name} [{proto_name}] ({msg.dlc} bytes, {len(msg.signals)} signals)")
            for e in entry_errors:
                print(e)
            total_errors += len(entry_errors)
        elif verbose:
            print(f"  OK: {id_str} {msg_name} [{proto_name}] ({msg.dlc} bytes, {len(msg.signals)} signals)")

    # ── Summary ────────────────────────────────────────────────────────
    print(f"\n{'='*70}")
    if total_errors == 0:
        print(f"  PASS — All {total_checks} CAN messages match docs/io-autoware.md")
    else:
        print(f"  FAIL — {total_errors} error(s) across {total_checks} messages")
    print(f"{'='*70}")

    return 0 if total_errors == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
