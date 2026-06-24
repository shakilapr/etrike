#!/usr/bin/env python3
"""
Generate rt-esp32/syntree_eps.dbc using canmatrix.

Covers all SYNTREE EPS-C (steering-by-wire) CAN messages.
All frames use Intel (little-endian) format per SYNTREE protocol.
Preprogrammed CAN IDs — not reconfigurable.

  Command: 0x169 at 50 Hz continuous (RT -> EPS_C)
  Status:  0x201 at 100 Hz (EPS_C -> RT)
  Errors:  0x202 at 10 Hz (EPS_C -> RT)
  Version: 0x203 at 1 Hz (EPS_C -> RT)
  Test:    0x6FA at 100 Hz (EPS_C -> RT)

Rolling counter 0-15. Checksum: XOR(bytes 0-6) ^ 0xFF.

Note: Manufacturer CSV declares overlapping signals at byte 5 of 0x169/0x201
(StrAngleSpd[15:8] shares with security nibble). These are mode-dependent —
the EPS-C may report them in alternate frames. Both are included in the DBC
for documentation; tools may warn about the overlap.

Usage:
  python generate_eps_dbc.py           # writes syntree_eps.dbc
  python generate_eps_dbc.py --check   # writes + re-parses to validate
"""

import os
import sys
from io import BytesIO

import canmatrix
import canmatrix.formats.dbc
from canmatrix import CanMatrix, Ecu, Frame, Signal, ArbitrationId


OUTPUT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_FILE = os.path.join(OUTPUT_DIR, "syntree_eps.dbc")

LE = True   # Intel (little-endian) — all SYNTREE frames


def build_database() -> CanMatrix:
    """Construct the SYNTREE EPS-C CAN database."""

    db = CanMatrix()

    db.add_ecu(Ecu("RT",    "ESP32-S3, FreeRTOS — drives EPS-C via 0x169 at 50 Hz"))
    db.add_ecu(Ecu("EPS_C", "SYNTREE EPS-C, steer-by-wire module, preprogrammed CAN IDs"))

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

    # ── 0x169 — VCU_SES_REQ (RT -> EPS_C, 50 Hz continuous) ────────────
    db.add_frame(msg("VCU_SES_REQ", 0x169, 8, "RT", cycle_ms=20,
        signals=[
            sig("SES_AlignEnable",    0,  1, vmin=0, vmax=1, receivers=["EPS_C"],
                comment="Angle Initial Alignment Enable. 0=disabled, 1=centering."),
            sig("SES_CtrlEnable",     1,  1, vmin=0, vmax=1, receivers=["EPS_C"],
                comment="Direction Control Enable. 0=Disabled, 1=Enable (Angle Control)."),
            sig("SES_TgtStrAngle",   16, 16, signed=True, factor=0.1, offset=-3000,
                vmin=-700, vmax=700, unit="deg", receivers=["EPS_C"],
                comment="Target Steering Angle. Negative=left. Offset=-3000 per mfr CSV."),
            sig("SES_TgtStrAngleSpd", 32, 16, factor=1, offset=0,
                vmin=125, vmax=525, unit="deg/s", receivers=["EPS_C"],
                comment="Target Angle Speed. Byte 5 overlaps with security signals per CSV."),
            sig("SES_RollCntEnable", 40,  1, vmin=0, vmax=1, receivers=["EPS_C"],
                comment="Life Signal Enable — MUST be 1."),
            sig("SES_ChecksumEnable", 41,  1, vmin=0, vmax=1, receivers=["EPS_C"],
                comment="Checksum Enable — MUST be 1."),
            sig("SES_RollCnt",        44,  4, vmin=0, vmax=15, receivers=["EPS_C"],
                comment="Life Signal rolling counter. Increment every frame."),
            sig("SES_VehSpd",         48,  8, factor=1, offset=0,
                vmin=0, vmax=255, unit="km/h", receivers=["EPS_C"],
                comment="Vehicle speed populated by RT."),
            sig("SES_Checksum",       56,  8, vmin=0, vmax=255, receivers=["EPS_C"],
                comment="Checksum = XOR(bytes 0-6) ^ 0xFF."),
        ],
        comment="SYNTREE EPS-C command. 50 Hz continuous. "
                "Byte 5 overlap: Speed[15:8] shares with security nibble per CSV.",
    ))

    # ── 0x201 — SES_STATUS (EPS_C -> RT, 100 Hz) ────────────────────────
    db.add_frame(msg("SES_STATUS", 0x201, 8, "EPS_C", cycle_ms=10,
        signals=[
            sig("SES_AngleStatus",       0,  1, vmin=0, vmax=1, receivers=["RT"],
                comment="Center Finding Status. 0=Finding, 1=Found."),
            sig("SES_CtrlModeStatus",     1,  2, vmin=0, vmax=3, receivers=["RT"],
                comment="Control Mode Feedback. 0=Manual, 1=Automatic."),
            sig("SES_ErrorStatus",        6,  2, vmin=0, vmax=3, unit="enum", receivers=["RT"],
                values={0: "Normal", 1: "L1_Warning", 2: "L2_General", 3: "L3_Severe"},
                comment="Error Status."),
            sig("SES_StrAngle",          16, 16, factor=0.1, offset=-3000,
                vmin=-700, vmax=700, unit="deg", receivers=["RT"],
                comment="Steering Angle. Raw 30000->0deg, 23000->-700deg, 37000->700deg."),
            sig("SES_TgtStrAngleSpd_FB", 32, 16, signed=True, factor=0.5, offset=0,
                vmin=0, vmax=1480, unit="deg/s", receivers=["RT"],
                comment="Angle Speed feedback. 16-bit signed. Overlaps Torq at byte 5."),
            sig("SES_SteeringTorq",      40,  8, factor=0.1, offset=-12.1,
                vmin=-12, vmax=12, unit="Nm", receivers=["RT"],
                comment="Steering Torque. Init 0x79 (121 raw = 0 Nm)."),
            sig("SES_RollCntEnStatus",   48,  1, vmin=0, vmax=1, receivers=["RT"],
                comment="Life Signal Enable Feedback."),
            sig("SES_ChecksumEnStatus",  49,  1, vmin=0, vmax=1, receivers=["RT"],
                comment="Checksum Enable Feedback."),
            sig("SES_RollCntStatus",     52,  4, vmin=0, vmax=15, receivers=["RT"],
                comment="Life Signal Feedback — echoes rolling counter."),
            sig("SES_ChecksumStatus",    56,  8, vmin=0, vmax=255, receivers=["RT"],
                comment="Checksum Feedback = XOR(bytes 0-6) ^ 0xFF."),
        ],
        comment="SYNTREE EPS-C status feedback. 100 Hz. "
                "Byte 5 overlap: StrAngleSpd[15:8] / Torq share byte 5 per CSV.",
    ))

    # ── 0x202 — SES_ErrInfo (EPS_C -> RT, 10 Hz) ────────────────────────
    db.add_frame(msg("SES_ErrInfo", 0x202, 8, "EPS_C", cycle_ms=100,
        signals=[
            sig("SES_ECUUnderVolt",  0, 1, vmin=0, vmax=1, receivers=["RT"], comment="Controller Under Voltage [L2]"),
            sig("SES_ECUOverVolt",   1, 1, vmin=0, vmax=1, receivers=["RT"], comment="Controller Over Voltage [L2]"),
            sig("SES_CanComErr",     2, 1, vmin=0, vmax=1, receivers=["RT"], comment="CAN Communication Fault [L1]"),
            sig("SES_ECUTempErr",    3, 1, vmin=0, vmax=1, receivers=["RT"], comment="Controller Temp Fault [L1]"),
            sig("SES_DomainSC",      4, 1, vmin=0, vmax=1, receivers=["RT"], comment="Domain Drive Short Circuit [L2]"),
            sig("SES_DomainV",       5, 1, vmin=0, vmax=1, receivers=["RT"], comment="Domain Drive Voltage Fault [L2]"),
            sig("SES_DomainT",       6, 1, vmin=0, vmax=1, receivers=["RT"], comment="Domain Drive Temperature Fault [L2]"),
            sig("SES_TempSensor",    7, 1, vmin=0, vmax=1, receivers=["RT"], comment="Temperature Sensor Fault"),
            sig("SES_AngleP_OC",     8, 1, vmin=0, vmax=1, receivers=["RT"], comment="Angle Sensor Pri. Open Circuit [L3]"),
            sig("SES_AngleP_AF",     9, 1, vmin=0, vmax=1, receivers=["RT"], comment="Angle Sensor Pri. Out of Range [L3]"),
            sig("SES_AngleS_OC",    10, 1, vmin=0, vmax=1, receivers=["RT"], comment="Angle Sensor Sec. Open Circuit [L3]"),
            sig("SES_AngleS_AF",    11, 1, vmin=0, vmax=1, receivers=["RT"], comment="Angle Sensor Sec. Out of Range [L3]"),
            sig("SES_SensorPow",    12, 1, vmin=0, vmax=1, receivers=["RT"], comment="Sensor Power Fault [L2]"),
            sig("SES_Alignment",    13, 1, vmin=0, vmax=1, receivers=["RT"], comment="Centering Fault [L1]"),
            sig("SES_OverAngle",    14, 1, vmin=0, vmax=1, receivers=["RT"], comment="Over Angle Fault [L2]"),
            sig("SES_StrMtrStall",  15, 1, vmin=0, vmax=1, receivers=["RT"], comment="Motor Stall Fault [L1]"),
            sig("SES_MtrCurt",      16, 1, vmin=0, vmax=1, receivers=["RT"], comment="Motor Current Fault [L2]"),
            sig("SES_SensorCL",     17, 1, vmin=0, vmax=1, receivers=["RT"], comment="Sensor 5V Power Fault [L2]"),
            sig("SES_TorqT1_OC",    18, 1, vmin=0, vmax=1, receivers=["RT"], comment="Torque Sensor T1 Open Circuit [L3]"),
            sig("SES_TorqT1_AF",    19, 1, vmin=0, vmax=1, receivers=["RT"], comment="Torque Sensor T1 Out of Range [L3]"),
            sig("SES_TorqT2_OC",    20, 1, vmin=0, vmax=1, receivers=["RT"], comment="Torque Sensor T2 Open Circuit [L3]"),
            sig("SES_TorqT2_AF",    21, 1, vmin=0, vmax=1, receivers=["RT"], comment="Torque Sensor T2 Out of Range [L3]"),
            sig("SES_SentAngle",    22, 1, vmin=0, vmax=1, receivers=["RT"], comment="Angle Error [L1]"),
            sig("SES_StrMtrIdling", 23, 1, vmin=0, vmax=1, receivers=["RT"], comment="Motor Idling Fault [L2]"),
            sig("SES_EPROM",        24, 1, vmin=0, vmax=1, receivers=["RT"], comment="EEPROM Fault [L2]"),
            sig("SES_VehSpdSnapshot", 56, 8, factor=1, offset=0,
                vmin=0, vmax=255, unit="km/h", receivers=["RT"],
                comment="Vehicle speed at fault snapshot."),
        ],
        comment="SYNTREE EPS-C detailed fault flags. 8 L3 faults (redundant sensor loss) "
                "-> RT must escalate to ESTOP. Steering CAN comm is L1 (minor).",
    ))

    # ── 0x203 — SES_Version (EPS_C -> RT, 1 Hz) ─────────────────────────
    db.add_frame(msg("SES_Version", 0x203, 8, "EPS_C", cycle_ms=1000,
        signals=[
            sig("SES_SW_Version", 0, 8, factor=0.01, offset=0,
                vmin=0, vmax=2.55, receivers=["RT"],
                comment="Software version (e.g. 0x64 = 1.00)"),
            sig("SES_HW_Version", 8, 8, factor=0.1, offset=0,
                vmin=0, vmax=25.5, receivers=["RT"],
                comment="Hardware version (e.g. 0x0D = 1.3)"),
        ],
        comment="SYNTREE EPS-C firmware version. Log on boot for compatibility check.",
    ))

    # ── 0x6FA — SES_Test (EPS_C -> RT, 100 Hz telemetry) ────────────────
    db.add_frame(msg("SES_Test", 0x6FA, 8, "EPS_C", cycle_ms=10,
        signals=[
            sig("SES_MtrCurt",    8, 16, signed=True, factor=0.0078125, offset=0,
                vmin=0, vmax=60, unit="A", receivers=["RT"],
                comment="Motor current. Monitor for mechanical binding / rack damage."),
            sig("SES_ECUTemp",   24, 16, factor=0.5, offset=0,
                vmin=0, vmax=255, unit="degC", receivers=["RT"],
                comment="ECU temperature. For thermal throttling."),
            sig("SES_PowVolt",   40, 16, factor=0.00390625, offset=0,
                vmin=0, vmax=18, unit="V", receivers=["RT"],
                comment="Supply voltage. 0-18V range."),
        ],
        comment="SYNTREE EPS-C telemetry. 100 Hz. Bytes 0,7 reserved. "
                "Narrower ranges than brake SEB_Test.",
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
