#!/usr/bin/env python3
"""
Generate shared/can/etrike_custom.dbc using canmatrix.

Covers all custom (non-SYNTREE) CAN messages across both buses:
  High bus: Jetson <-> RT, 500 kbit/s, Motorola (big-endian) format
  Low bus:  RT/SYS/MTR/DCDC, 500 kbit/s, Motorola (big-endian) format

Usage:
  python generate_dbc.py           # writes etrike_custom.dbc
  python generate_dbc.py --check   # writes + re-parses to validate
"""

import os
import sys
from io import BytesIO

import canmatrix
import canmatrix.formats.dbc
from canmatrix import CanMatrix, Ecu, Frame, Signal, ArbitrationId


OUTPUT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_FILE = os.path.join(OUTPUT_DIR, "etrike_custom.dbc")

# Motorola (big-endian) — canmatrix uses LSB start_bit convention internally
# and converts to MSB convention in DBC output. Both are physically equivalent.
LE = True   # little-endian (Intel)
BE = False  # big-endian (Motorola)


def build_database() -> CanMatrix:
    """Construct the E-Trike custom CAN database."""

    db = CanMatrix()

    # ── Nodes ──────────────────────────────────────────────────────────
    db.add_ecu(Ecu("Jetson", "Orin NX, Linux + ROS 2, perception/planning"))
    db.add_ecu(Ecu("RT",     "ESP32-S3, FreeRTOS, realtime kinematics + CAN gateway"))
    db.add_ecu(Ecu("SYS",    "ESP32-S3, FreeRTOS, safety + motor + body control"))
    db.add_ecu(Ecu("MTR",    "STM32, bare metal, motor DAC + gear relays (EGAS L1)"))
    db.add_ecu(Ecu("DCDC",   "DC-DC converter (72V->12V), CAN 0x012 control"))

    # ── Helper ─────────────────────────────────────────────────────────
    def sig(name, start, size, signed=False, factor=1, offset=0,
            vmin=None, vmax=None, unit="", receivers=None, comment="",
            values=None, le=BE):
        # Default min/max to full bit range if not specified
        if vmin is None:
            vmin = -(1 << (size - 1)) if signed else 0
        if vmax is None:
            vmax = (1 << (size - 1)) - 1 if signed else (1 << size) - 1
        s = canmatrix.Signal(
            name, start_bit=start, size=size,
            is_little_endian=le, is_signed=signed,
            factor=factor, offset=offset,
            min=vmin, max=vmax,
            unit=unit, receivers=receivers or [], comment=comment,
        )
        if values:
            s.values = values
        return s

    def msg(name, fid, dlc, sender, cycle_ms=0, signals=None, comment=""):
        f = canmatrix.Frame(
            name, arbitration_id=ArbitrationId(fid),
            size=dlc, transmitters=[sender],
            cycle_time=cycle_ms, comment=comment,
        )
        for s in signals or []:
            f.add_signal(s)
        return f

    # ── Messages ───────────────────────────────────────────────────────

    db.add_frame(msg("SAFETY_ESTOP", 0x001, 0, "RT", cycle_ms=0, signals=[],
                     comment="DLC=0 — the frame ID itself is the ESTOP signal. "
                             "Bridged bidirectionally by RT. Highest priority CAN frame."))

    db.add_frame(msg("SYS_SAFETY_STS", 0x011, 2, "SYS", cycle_ms=200,
        signals=[
            sig("SYS_EstopActive", 0, 8, vmin=0, vmax=1, receivers=["RT", "Jetson"]),
            sig("SYS_HeartbeatOk", 8, 8, vmin=0, vmax=1, receivers=["RT", "Jetson"],
                comment="0=RT alive counter frozen >1000ms, 1=incrementing"),
        ],
        comment="Forwarded low->high by RT. Same payload on both buses.",
    ))

    db.add_frame(msg("SYS_DCDC_CMD", 0x012, 1, "SYS", cycle_ms=100,
        signals=[
            sig("SYS_DcdcEnable", 0, 8, vmin=0, vmax=1, receivers=["DCDC"],
                comment="ESTOP->1(on); maintains 12V for MCUs, CAN transceivers, brake light"),
        ],
        comment="DC-DC converter control. Low bus only.",
    ))

    db.add_frame(msg("SYS_MODE_CMD", 0x110, 1, "SYS", cycle_ms=0,
        signals=[
            sig("SYS_Mode", 0, 8, vmin=0, vmax=2, unit="enum", receivers=["RT"],
                values={0: "Manual", 1: "Auto", 2: "ESTOP"}),
        ],
        comment="0=Manual, 1=Auto, 2=ESTOP. Low bus only.",
    ))

    db.add_frame(msg("SYS_THROTTLE_STS", 0x120, 2, "MTR", cycle_ms=10,
        signals=[
            sig("SYS_ThrottleSpeed", 0, 16, signed=True, factor=1, offset=0,
                vmin=-500, vmax=3000, unit="mm/s", receivers=["RT", "Jetson"]),
        ],
        comment="Current vehicle speed from MTR STM32. Forwarded low->high by RT.",
    ))

    db.add_frame(msg("RT_DRIVE_CMD", 0x204, 5, "RT", cycle_ms=10,
        signals=[
            sig("RT_MotorSpeed", 0, 32, signed=True, factor=1, offset=0,
                vmin=-500, vmax=3000, unit="mm/s", receivers=["SYS", "MTR"]),
            sig("RT_Gear", 32, 8, vmin=0, vmax=3, unit="enum", receivers=["SYS", "MTR"],
                values={0: "N", 1: "D", 2: "S", 3: "R"}),
        ],
        comment="MTR receives for motor actuation. SYS receives for EGAS L2 monitoring. "
                "ID 0x204 avoids collision with EPS-C 0x202.",
    ))

    db.add_frame(msg("RT_BRAKE_CMD", 0x205, 4, "RT", cycle_ms=20,
        signals=[
            sig("RT_BrakePressure", 0, 32, signed=True, factor=1, offset=0,
                vmin=0, vmax=20000, unit="kPa", receivers=["SYS"]),
        ],
        comment="RT max-select: max(rt_obstacle, jetson_0x301) -> SYS SEB cmd.",
    ))

    db.add_frame(msg("MTR_MOTOR_FBK", 0x206, 4, "MTR", cycle_ms=20,
        signals=[
            sig("MTR_ActualSpeed", 0, 16, signed=True, factor=1, offset=0,
                vmin=-500, vmax=3000, unit="mm/s", receivers=["RT", "SYS"]),
            sig("MTR_GearState", 16, 8, vmin=0, vmax=3, unit="enum", receivers=["RT", "SYS"],
                values={0: "N", 1: "D", 2: "S", 3: "R"}),
            sig("MTR_FaultFlags", 24, 8, vmin=0, vmax=255, receivers=["RT", "SYS"],
                values={1: "Overcurrent", 2: "Overtemp", 4: "CommsLoss"},
                comment="bit0=ESTOP, bit1=CMD timeout, bit2=ADC fault, bit3=gear conflict"),
        ],
        comment="Motor feedback from STM32. Low bus only.",
    ))

    db.add_frame(msg("RT_STATE_RPT", 0x210, 3, "RT", cycle_ms=100,
        signals=[
            sig("RT_Mode", 0, 8, vmin=0, vmax=2, unit="enum", receivers=["Jetson"],
                values={0: "Manual", 1: "Auto", 2: "ESTOP"}),
            sig("RT_SteerValid", 8, 8, vmin=0, vmax=1, receivers=["Jetson"]),
            sig("RT_Reversing", 16, 8, vmin=0, vmax=1, receivers=["Jetson"]),
        ],
        comment="RT state report to Jetson. High bus only.",
    ))

    db.add_frame(msg("RT_PID_RPT", 0x220, 6, "RT", cycle_ms=100,
        signals=[
            sig("RT_PidSetpoint", 0, 16, signed=True, unit="mm/s", receivers=["Jetson"]),
            sig("RT_PidMeasured", 16, 16, signed=True, unit="mm/s", receivers=["Jetson"]),
            sig("RT_PidOutput", 32, 16, signed=True, receivers=["Jetson"]),
        ],
        comment="RESERVED, inactive. PID telemetry for Jetson debugging.",
    ))

    db.add_frame(msg("HOST_DRIVE_CMD", 0x300, 8, "Jetson", cycle_ms=10,
        signals=[
            sig("HOST_DriveSpeed", 0, 32, signed=True, factor=1, offset=0,
                vmin=-500, vmax=3000, unit="mm/s", receivers=["RT"],
                comment="ROS 2: linear.x * 1000"),
            sig("HOST_YawRate", 32, 24, signed=True, factor=1, offset=0,
                vmin=-3000, vmax=3000, unit="mrad/s", receivers=["RT"],
                comment="ROS 2: angular.z * 1000"),
            sig("HOST_Gear", 56, 8, vmin=0, vmax=3, unit="enum", receivers=["RT"],
                values={0: "N", 1: "D", 2: "S", 3: "R"}),
        ],
        comment="Jetson Autoware.Auto drive command -> RT. High bus only.",
    ))

    db.add_frame(msg("HOST_BRAKE_REQ", 0x301, 4, "Jetson", cycle_ms=0,
        signals=[
            sig("HOST_BrakePressure", 0, 32, signed=True, factor=1, offset=0,
                vmin=0, vmax=20000, unit="kPa", receivers=["RT"]),
        ],
        comment="On demand. RT arbitrates: max(RT_computed, HOST_request) -> 0x205.",
    ))

    db.add_frame(msg("HOST_LIGHT_CMD", 0x302, 1, "Jetson", cycle_ms=0,
        signals=[
            sig("HOST_LeftTurn",   0, 1, vmin=0, vmax=1, receivers=["RT", "SYS"]),
            sig("HOST_RightTurn",  1, 1, vmin=0, vmax=1, receivers=["RT", "SYS"]),
            sig("HOST_BrakeLight", 2, 1, vmin=0, vmax=1, receivers=["RT", "SYS"]),
            sig("HOST_Headlight",  3, 1, vmin=0, vmax=1, receivers=["RT", "SYS"]),
        ],
        comment="Forwarded transparently high->low by RT.",
    ))

    db.add_frame(msg("RT_OBSTACLE_RPT", 0x400, 4, "RT", cycle_ms=100,
        signals=[
            sig("RT_ObstacleDistance", 0, 32, vmin=0, vmax=4294967295, unit="mm",
                receivers=["Jetson"], comment="UINT32_MAX = no reading / timeout"),
        ],
        comment="RT reports min obstacle distance to Jetson at 10 Hz. High bus only.",
    ))

    db.add_frame(msg("SYS_DIAG_RPT", 0x600, 8, "SYS", cycle_ms=1000,
        signals=[
            sig("SYS_DiagMode",          0,  8, vmin=0, vmax=2, receivers=["RT", "Jetson"]),
            sig("SYS_DiagBrakeEngaged",  8,  8, vmin=0, vmax=1, receivers=["RT", "Jetson"]),
            sig("SYS_DiagHeartbeatOk",  16,  8, vmin=0, vmax=1, receivers=["RT", "Jetson"]),
            sig("SYS_DiagEstopActive",  24,  8, vmin=0, vmax=1, receivers=["RT", "Jetson"]),
            sig("SYS_DiagFreeHeapKb",   32, 16, vmin=0, vmax=65535, unit="KB", receivers=["RT", "Jetson"]),
            sig("SYS_DiagTec",          48,  8, vmin=0, vmax=255, receivers=["RT", "Jetson"]),
            sig("SYS_DiagRec",          56,  8, vmin=0, vmax=255, receivers=["RT", "Jetson"]),
        ],
        comment="SYS diagnostics report. Forwarded low->high by RT.",
    ))

    db.add_frame(msg("JETSON_HEARTBEAT", 0x7FC, 1, "Jetson", cycle_ms=500,
        signals=[
            sig("Jetson_AliveCtr", 0, 8, vmin=0, vmax=255, receivers=["RT"],
                comment="Timeout 1500ms -> controlled stop. Jetson is QM, not safety-critical."),
        ],
        comment="Not bridged, high bus only. Loss triggers controlled stop, not ESTOP.",
    ))

    db.add_frame(msg("RT_HEARTBEAT", 0x7FD, 1, "RT", cycle_ms=500,
        signals=[
            sig("RT_AliveCtr", 0, 8, vmin=0, vmax=255, receivers=["Jetson", "SYS"],
                comment="Low bus timeout 1000ms->SYS ESTOP; High bus 1500ms->Jetson stops /cmd_vel"),
        ],
        comment="RT sends independently on both buses (per-bus, NOT bridged). Separate counters.",
    ))

    db.add_frame(msg("SYS_HEARTBEAT", 0x7FE, 1, "SYS", cycle_ms=100,
        signals=[
            sig("SYS_AliveCtr", 0, 8, vmin=0, vmax=255, receivers=["RT"],
                comment="10 Hz / 200ms timeout -> RT brake takeover + ESTOP"),
        ],
        comment="Low bus only, never leaves low bus.",
    ))

    return db


def main():
    db = build_database()

    if "--check" in sys.argv:
        # Write DBC
        buf = BytesIO()
        canmatrix.formats.dbc.dump(db, buf)
        dbc_text = buf.getvalue().decode("utf-8")
        os.makedirs(os.path.dirname(OUTPUT_FILE), exist_ok=True)
        with open(OUTPUT_FILE, "wb") as f:
            f.write(buf.getvalue())
        print(f"Wrote {OUTPUT_FILE} ({len(dbc_text)} bytes)")

        # Re-parse to validate
        with open(OUTPUT_FILE, "rb") as fh:
            db2 = canmatrix.formats.dbc.load(fh, dbcImportEncoding="utf-8")
        print(f"Validated: {len(db2.frames)} frames, {len(db2.ecus)} ECUs")
        for f in db2.frames:
            print(f"  0x{f.arbitration_id.id:03X} {f.name} ({len(f.signals)} signals)")

        # Smoke test: encode/decode a few frames
        print()
        for name, data in [
            ("HOST_DRIVE_CMD", {"HOST_DriveSpeed": 1500, "HOST_YawRate": 500, "HOST_Gear": 2}),
            ("MTR_MOTOR_FBK", {"MTR_ActualSpeed": 1200, "MTR_GearState": 1, "MTR_FaultFlags": 0}),
        ]:
            f = db2.frame_by_name(name)
            enc = f.encode(data)
            dec = f.decode(enc)
            for k, v in data.items():
                phys = dec[k].phys_value
                ok = abs(phys - v) < 1
                print(f"  {name}.{k}: {v} -> encode -> decode -> {phys:.0f} {'OK' if ok else 'FAIL'}")
    else:
        buf = BytesIO()
        canmatrix.formats.dbc.dump(db, buf)
        os.makedirs(os.path.dirname(OUTPUT_FILE), exist_ok=True)
        with open(OUTPUT_FILE, "wb") as f:
            f.write(buf.getvalue())
        dbc_text = buf.getvalue().decode("utf-8")
        print(f"Wrote {OUTPUT_FILE} ({len(dbc_text)} bytes)")
        print(f"  {len(db.frames)} frames, {len(db.ecus)} ECUs")


if __name__ == "__main__":
    main()
