#!/usr/bin/env python3
"""
Generate sys-esp32/syntree_seb.dbc using canmatrix.

Covers all SYNTREE SEB (electro-hydraulic brake) CAN messages.
All frames use Intel (little-endian) format per SYNTREE protocol.
Preprogrammed CAN IDs — not reconfigurable.

  Command: 0x7B9 at 50 Hz continuous (SYS -> SEB)
  Status:  0x721 at 100 Hz (SEB -> SYS)
  Errors:  0x731 at 10 Hz (SEB -> SYS)
  Version: 0x741 at 1 Hz (SEB -> SYS)
  Test:    0x6FB at 100 Hz (SEB -> SYS)

Rolling counter 0-15. Checksum: XOR(bytes 0-6) ^ 0xFF.

Note: Manufacturer CSV declares mode-muxed overlapping signals at byte 3
of 0x7B9/0x721 (Stroke[15:8] shares with Pressure). These are mode-dependent —
Stroke uses byte 3 in Mode 0, Pressure uses it in Mode 1. Both are included
in the DBC for documentation.

Usage:
  python generate_seb_dbc.py           # writes syntree_seb.dbc
  python generate_seb_dbc.py --check   # writes + re-parses to validate
"""

import os
import sys
from io import BytesIO

import canmatrix
import canmatrix.formats.dbc
from canmatrix import CanMatrix, Ecu, Frame, Signal, ArbitrationId


OUTPUT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_FILE = os.path.join(OUTPUT_DIR, "syntree_seb.dbc")

LE = True   # Intel (little-endian) — all SYNTREE frames


def build_database() -> CanMatrix:
    """Construct the SYNTREE SEB CAN database."""

    db = CanMatrix()

    db.add_ecu(Ecu("SYS", "ESP32-S3, FreeRTOS — drives SEB via 0x7B9 at 50 Hz"))
    db.add_ecu(Ecu("SEB", "SYNTREE SEB, electro-hydraulic brake module, preprogrammed CAN IDs"))

    def sig(name, start, size, signed=False, factor=1, offset=0,
            vmin=None, vmax=None, unit="", receivers=None, comment="",
            values=None):
        if vmin is None:
            vmin = -(1 << (size - 1)) if signed else 0
        if vmax is None:
            vmax = (1 << (size - 1)) - 1 if signed else (1 << size) - 1
        s = Signal(name, start_bit=start, size=size,
                   is_little_endian=LE, is_signed=signed,
                   factor=factor, offset=offset,
                   min=vmin, max=vmax, unit=unit,
                   receivers=receivers or [], comment=comment)
        if values:
            s.values = values
        return s

    def msg(name, fid, dlc, sender, cycle_ms=0, signals=None, comment=""):
        f = Frame(name, arbitration_id=ArbitrationId(fid),
                  size=dlc, transmitters=[sender],
                  cycle_time=cycle_ms, comment=comment)
        for s in signals or []:
            f.add_signal(s)
        return f

    # ── 0x7B9 — VCU_SEB_REQ (SYS -> SEB, 50 Hz continuous) ─────────────
    db.add_frame(msg("VCU_SEB_REQ", 0x7B9, 8, "SYS", cycle_ms=20,
        signals=[
            sig("SEB_AlignEnable",   0,  1, vmin=0, vmax=1, receivers=["SEB"],
                comment="Calibration enable."),
            sig("SEB_CtrlEnable",    1,  1, vmin=0, vmax=1, receivers=["SEB"],
                comment="Active control enable."),
            sig("SEB_CtrlMode",      2,  1, vmin=0, vmax=1, unit="enum", receivers=["SEB"],
                values={0: "Stroke", 1: "Pressure"},
                comment="0=Stroke (position), 1=Pressure (hydraulic)."),
            sig("SEB_AutoBrake",     3,  1, vmin=0, vmax=1, receivers=["SEB"],
                comment="Auto-brake / emergency trigger."),
            sig("SEB_StrokeReq",    16, 16, factor=0.05, offset=-30,
                vmin=-5, vmax=27, unit="mm", receivers=["SEB"],
                comment="Stroke position. Raw = (mm + 30.0) / 0.05. "
                        "Range: 500(-5mm) to 1140(27mm ESTOP)."),
            sig("SEB_PressureReq",  24,  8, factor=0.05, offset=0,
                vmin=0, vmax=5, unit="MPa", receivers=["SEB"],
                comment="Pressure in Mode 1. Raw = kPa * 0.02. Overlaps Stroke[15:8] — mode-dependent."),
            sig("SEB_RollCntEnable", 48,  1, vmin=0, vmax=1, receivers=["SEB"],
                comment="Life Signal Validity — MUST be 1."),
            sig("SEB_ChecksumEnable", 49, 1, vmin=0, vmax=1, receivers=["SEB"],
                comment="Checksum Validity — MUST be 1."),
            sig("SEB_RollCnt",       52,  4, vmin=0, vmax=15, receivers=["SEB"],
                comment="Life Signal rolling counter. Increment every frame."),
            sig("SEB_Checksum",      56,  8, vmin=0, vmax=255, receivers=["SEB"],
                comment="Checksum = XOR(bytes 0-6) ^ 0xFF."),
        ],
        comment="SYNTREE SEB brake command. 50 Hz continuous. "
                "Byte 3 mode-mux: Stroke[15:8] in Mode 0, Pressure in Mode 1.",
    ))

    # ── 0x721 — SEB_STATUS (SEB -> SYS, 100 Hz) ────────────────────────
    db.add_frame(msg("SEB_STATUS", 0x721, 8, "SEB", cycle_ms=10,
        signals=[
            sig("SEB_AlignStatus",       0,  1, vmin=0, vmax=1, receivers=["SYS"],
                comment="Alignment Info Feedback. 1=aligned."),
            sig("SEB_CtrlEnStatus",      1,  1, vmin=0, vmax=1, receivers=["SYS"],
                comment="Control Enable Feedback."),
            sig("SEB_CtrlModeStatus",    2,  2, vmin=0, vmax=3, receivers=["SYS"],
                values={0: "None", 1: "Stroke", 2: "Pressure"},
                comment="Control Mode Feedback."),
            sig("SEB_AutoBrakeStatus",   4,  1, vmin=0, vmax=1, receivers=["SYS"],
                comment="Auto Brake Status Feedback."),
            sig("SEB_ErrorStatus",       6,  2, vmin=0, vmax=3, unit="enum", receivers=["SYS"],
                values={0: "Normal", 1: "L1_Warning", 2: "L2_General", 3: "L3_Severe"},
                comment="Error Status."),
            sig("SEB_StrokeValue",      16, 16, factor=0.05, offset=-30,
                vmin=-5, vmax=27, unit="mm", receivers=["SYS"],
                comment="Stroke Value Feedback."),
            sig("SEB_PressureValue",    24,  8, factor=0.05, offset=0,
                vmin=0, vmax=5, unit="MPa", receivers=["SYS"],
                comment="Pressure Feedback. Overlaps Stroke[15:8] — mode-dependent."),
            sig("SEB_AngleValue",       40, 16, signed=True, factor=0.5, offset=0,
                vmin=-150, vmax=840, receivers=["SYS"],
                comment="Angle Feedback. Overlaps security echo at byte 6."),
            sig("SEB_RollCntEnStatus",  48,  1, vmin=0, vmax=1, receivers=["SYS"],
                comment="Life Signal Status Feedback."),
            sig("SEB_ChecksumEnStatus", 49,  1, vmin=0, vmax=1, receivers=["SYS"],
                comment="Checksum Status Feedback."),
            sig("SEB_RollCntStatus",    52,  4, vmin=0, vmax=15, receivers=["SYS"],
                comment="Life Signal Feedback — echoes rolling counter."),
            sig("SEB_ChecksumStatus",   56,  8, vmin=0, vmax=255, receivers=["SYS"],
                comment="Checksum Feedback."),
        ],
        comment="SYNTREE SEB status feedback. 100 Hz. "
                "SYS usage: boot sync -> read StrokeValue; active -> confirm AlignStatus==1.",
    ))

    # ── 0x731 — SEB_ErrInfo (SEB -> SYS, 10 Hz) ────────────────────────
    db.add_frame(msg("SEB_ErrInfo", 0x731, 8, "SEB", cycle_ms=100,
        signals=[
            sig("SEB_ECUUnderVolt",    0, 1, vmin=0, vmax=1, receivers=["SYS"], comment="Controller Undervoltage [L2]"),
            sig("SEB_ECUOverVolt",     1, 1, vmin=0, vmax=1, receivers=["SYS"], comment="Controller Overvoltage [L2]"),
            sig("SEB_CanComErr",       2, 1, vmin=0, vmax=1, receivers=["SYS"], comment="CAN Communication Fault [L3]"),
            sig("SEB_ECUTempErr",      3, 1, vmin=0, vmax=1, receivers=["SYS"], comment="Controller Temperature Fault [L3]"),
            sig("SEB_DomainSC",        4, 1, vmin=0, vmax=1, receivers=["SYS"], comment="Domain Drive Short Circuit [L3]"),
            sig("SEB_DomainV",         5, 1, vmin=0, vmax=1, receivers=["SYS"], comment="Domain Drive Voltage Fault [L3]"),
            sig("SEB_DomainT",         6, 1, vmin=0, vmax=1, receivers=["SYS"], comment="Domain Drive Temperature Fault [L3]"),
            sig("SEB_AngleP_OC",       7, 1, vmin=0, vmax=1, receivers=["SYS"], comment="Angle Sensor P Open Circuit [L3]"),
            sig("SEB_AngleP_AF",       8, 1, vmin=0, vmax=1, receivers=["SYS"], comment="Angle Sensor P Mainboard Abnormal [L3]"),
            sig("SEB_AngleS_OC",       9, 1, vmin=0, vmax=1, receivers=["SYS"], comment="Angle Sensor S Open Circuit [L3]"),
            sig("SEB_AngleS_AF",      10, 1, vmin=0, vmax=1, receivers=["SYS"], comment="Angle Sensor S Sub-board Abnormal [L3]"),
            sig("SEB_NoPreSensor",    11, 1, vmin=0, vmax=1, receivers=["SYS"], comment="Unconnected Oil Pressure Sensor [L3]"),
            sig("SEB_SensorUCL",      13, 1, vmin=0, vmax=1, receivers=["SYS"], comment="Sensor Plausibility Fault [L3]"),
            sig("SEB_AlignmentErr",   14, 1, vmin=0, vmax=1, receivers=["SYS"], comment="Alignment Fault [L2]"),
            sig("SEB_AngleOver",      15, 1, vmin=0, vmax=1, receivers=["SYS"], comment="Angle Out of Bounds [L2]"),
            sig("SEB_MtrStall",       17, 1, vmin=0, vmax=1, receivers=["SYS"], comment="Motor Stall Fault [L3]"),
            sig("SEB_MtrDC",          18, 1, vmin=0, vmax=1, receivers=["SYS"], comment="Motor Disconnect Fault [L3]"),
            sig("SEB_OilErr",         19, 1, vmin=0, vmax=1, receivers=["SYS"], comment="Oil Pressure Error [L2]"),
            sig("SEB_InitOil",        20, 1, vmin=0, vmax=1, receivers=["SYS"], comment="Initial Oil Pressure Fault [L3]"),
            sig("SEB_SentValue",      21, 1, vmin=0, vmax=1, receivers=["SYS"], comment="Send Value Error [L3]"),
            sig("SEB_MtrNoLoad",      22, 1, vmin=0, vmax=1, receivers=["SYS"], comment="Motor No-load Fault [L3]"),
            sig("SEB_PreSensorOver",  24, 1, vmin=0, vmax=1, receivers=["SYS"], comment="Oil Pressure Sensor Overvoltage [L2]"),
            sig("SEB_LowVoltCharging", 25, 1, vmin=0, vmax=1, receivers=["SYS"], comment="Low Voltage Charging Failure [L2]"),
        ],
        comment="SYNTREE SEB detailed fault flags. 14 of 23 faults are L3 -> "
                "SYS must escalate to ESTOP. Brake CAN comm loss is L3 (severe).",
    ))

    # ── 0x741 — SEB_Version (SEB -> SYS, 1 Hz) ─────────────────────────
    db.add_frame(msg("SEB_Version", 0x741, 8, "SEB", cycle_ms=1000,
        signals=[
            sig("SEB_SW_Version", 0, 8, factor=0.01, offset=0,
                vmin=0, vmax=2.55, receivers=["SYS"],
                comment="Software version (e.g. 0xC8 = 2.00)"),
            sig("SEB_HW_Version", 8, 8, factor=0.1, offset=0,
                vmin=0, vmax=25.5, receivers=["SYS"],
                comment="Hardware version (e.g. 0x0D = 1.3)"),
        ],
        comment="SYNTREE SEB firmware version. Log on boot for compatibility check.",
    ))

    # ── 0x6FB — SEB_Test (SEB -> SYS, 100 Hz telemetry) ────────────────
    db.add_frame(msg("SEB_Test", 0x6FB, 8, "SEB", cycle_ms=10,
        signals=[
            sig("SEB_MtrCurr",    8, 16, signed=True, factor=0.0078125, offset=0,
                vmin=-255, vmax=255, unit="A", receivers=["SYS"],
                comment="Motor current. Monitor for mechanical binding."),
            sig("SEB_ECUTemp",   24, 16, factor=0.5, offset=0,
                vmin=-40, vmax=215, unit="degC", receivers=["SYS"],
                comment="ECU temperature. For over-temperature early warning."),
            sig("SEB_PowVolt",   40, 16, factor=0.00390625, offset=0,
                vmin=0, vmax=32, unit="V", receivers=["SYS"],
                comment="Supply voltage. 0-32V range."),
        ],
        comment="SYNTREE SEB telemetry. 100 Hz. Bytes 0,7 reserved. "
                "Wider ranges than steering SES_Test.",
    ))

    return db


def main():
    db = build_database()

    if "--check" in sys.argv:
        buf = BytesIO()
        canmatrix.formats.dbc.dump(db, buf)
        os.makedirs(os.path.dirname(OUTPUT_FILE), exist_ok=True)
        with open(OUTPUT_FILE, "wb") as f:
            f.write(buf.getvalue())
        dbc_text = buf.getvalue().decode("utf-8")
        print(f"Wrote {OUTPUT_FILE} ({len(dbc_text)} bytes)")

        with open(OUTPUT_FILE, "rb") as fh:
            db2 = canmatrix.formats.dbc.load(fh, dbcImportEncoding="utf-8")
        print(f"Validated: {len(db2.frames)} frames, {len(db2.ecus)} ECUs")
        for f in db2.frames:
            print(f"  0x{f.arbitration_id.id:03X} {f.name} ({len(f.signals)} signals)")
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
