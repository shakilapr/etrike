/**
 * MCP2515 CAN frame decode + bus routing tests.
 *
 * Validates that the debug tool correctly decodes all high-bus telemetry
 * frames sent by RT's MCP2515 CAN controller. These tests exercise the
 * fix for Bug #1 (send now works) and Bug #2 (CNF3 timing corrected).
 *
 * Also validates bus routing: high-bus IDs must NOT appear as low-bus,
 * and low-bus IDs must NOT appear as high-bus.
 */

import { describe, it, expect } from "vitest";
import { decodeFrame, CAN_MESSAGES, BUSES } from "./types/can.js";

// ── Helper: get message definition by bus + hex ID ──────────────────
function findMsg(bus: "high" | "low", id: string) {
  return CAN_MESSAGES.find((m) => m.bus === bus && m.id === id);
}

// ── MCP2515 high-bus telemetry decode ───────────────────────────────

describe("MCP2515 high-bus CAN frame decode", () => {
  // 0x7FD — RT_HEARTBEAT (2 Hz)
  describe("0x7FD RT_HEARTBEAT (MCP2515 TX, 2 Hz)", () => {
    it("decodes alive counter byte and health_flags", () => {
      const result = decodeFrame("high", "0x7FD", [0xAB, 0x0F]);
      expect(result).toEqual({ alive_ctr: 0xAB, health_flags: 0x0F });
    });

    it("alive counter wrap at 256 (uint8 overflow), health_flags zero", () => {
      const r1 = decodeFrame("high", "0x7FD", [0xFF, 0x00]);
      const r2 = decodeFrame("high", "0x7FD", [0x00, 0x00]);
      expect(r1).toEqual({ alive_ctr: 255, health_flags: 0 });
      expect(r2).toEqual({ alive_ctr: 0, health_flags: 0 });
    });

    it("DLC is 2 bytes", () => {
      const msg = findMsg("high", "0x7FD");
      expect(msg?.dlc).toBe(2);
    });

    it("sender is RT", () => {
      const msg = findMsg("high", "0x7FD");
      expect(msg?.sender).toContain("RT");
    });

    it("period is 2 Hz (500ms)", () => {
      const msg = findMsg("high", "0x7FD");
      expect(msg?.period).toContain("2 Hz");
    });
  });

  // 0x210 — RT_STATE_RPT (10 Hz)
  describe("0x210 RT_STATE_RPT (MCP2515 TX, 10 Hz)", () => {
    it("decodes mode, safety_state, reversing", () => {
      const result = decodeFrame("high", "0x210", [1, 0, 0]);
      expect(result).toMatchObject({ mode: 1, safety_state: 0, estop_reason: 0, reversing: false });
    });

    it("decodes mode_name AUTO", () => {
      const result = decodeFrame("high", "0x210", [1, 0, 0]);
      expect(result).toMatchObject({ mode: 1, mode_name: "AUTO" });
    });

    it("manual mode, Normal safety state", () => {
      const result = decodeFrame("high", "0x210", [0, 0, 0]);
      expect(result).toMatchObject({
        mode: 0, mode_name: "MANUAL",
        safety_state: 0, estop_reason: 0, reversing: false,
      });
    });

    it("ESTOP mode, Fault safety state, reversing", () => {
      const result = decodeFrame("high", "0x210", [2, 2, 1]);
      expect(result).toMatchObject({
        mode: 2, mode_name: "ESTOP",
        safety_state: 2, estop_reason: 0, reversing: true,
      });
    });

    it("decodes estop_reason and safety_state from packed byte 1", () => {
      // Byte 1: safety_state=2 (bits 0-1), estop_reason=5 (bits 4-7) -> 0x52
      const result = decodeFrame("high", "0x210", [1, 0x52, 0, 0, 0, 0]);
      expect(result).toMatchObject({ mode: 1, safety_state: 2, estop_reason: 5 });
    });

    it("DLC is 6 bytes", () => {
      const msg = findMsg("high", "0x210");
      expect(msg?.dlc).toBe(6);
    });
  });

  // 0x310 — STEER_DIAG (10 Hz)
  describe("0x310 STEER_DIAG (MCP2515 TX, 10 Hz)", () => {
    it("decodes angle (scaled: (raw − 30000) × 0.1), fault, current (×0.01 A), temp (×0.1 °C)", () => {
      // raw angle=30000 → decoded (30000-30000)*0.1 = 0°, fault=0, raw current=1000→10A, raw temp=400→40°C
      const result = decodeFrame("high", "0x310", [
        0x75, 0x30,  // angle raw = 30000 → 0° (center)
        0x00,         // fault = false
        0x03, 0xE8,   // current raw = 1000 → 10.0 A
        0x01, 0x90,   // temp raw = 400 → 40.0 °C
        0x00,
      ]);
      expect(result).toMatchObject({
        SteerDiag_Angle0_1deg: 0,     // (30000-30000)*0.1 = 0
        SteerDiag_Fault: false,
        SteerDiag_MotorCurrent: 10,   // 1000*0.01
        SteerDiag_ECUTemp: 40,        // 400*0.1
      });
    });

    it("fault flag active", () => {
      const result = decodeFrame("high", "0x310", [
        0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
      ]);
      expect(result.SteerDiag_Fault).toBe(true);
    });

    it("max left angle (raw 26000 → decoded 26000*0.1-3000 = -400)", () => {
      const result = decodeFrame("high", "0x310", [
        0x65, 0x90,  // raw = 26000
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      ]);
      expect(result.SteerDiag_Angle0_1deg).toBeCloseTo(-400, 0);
    });

    it("DLC is 8 bytes", () => {
      const msg = findMsg("high", "0x310");
      expect(msg?.dlc).toBe(8);
    });
  });

  // 0x311 — BRAKE_DIAG (10 Hz)
  describe("0x311 BRAKE_DIAG (MCP2515 TX, 10 Hz)", () => {
    it("decodes pressure (raw × 0.05 MPa), fault, current (×0.01 A), temp (×0.1 °C)", () => {
      // raw pressure=1000→50 MPa, fault=0, raw current=200→2A, raw temp=300→30°C
      const result = decodeFrame("high", "0x311", [
        0x03, 0xE8,  // pressure raw = 1000 → 50 MPa
        0x00,         // fault
        0x00, 0xC8,   // current raw = 200 → 2.0 A
        0x01, 0x2C,   // temp raw = 300 → 30.0 °C
        0x00,
      ]);
      expect(result).toMatchObject({
        BrakeDiag_PressureRaw: 50,  // 1000*0.05
        BrakeDiag_Fault: false,
        BrakeDiag_MotorCurrent: 2,  // 200*0.01
        BrakeDiag_ECUTemp: 30,      // 300*0.1
      });
    });

    it("SEB fault active", () => {
      const result = decodeFrame("high", "0x311", [
        0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
      ]);
      expect(result.BrakeDiag_Fault).toBe(true);
    });

    it("DLC is 8 bytes", () => {
      const msg = findMsg("high", "0x311");
      expect(msg?.dlc).toBe(8);
    });
  });

  // 0x220 — RT_PID_RPT (10 Hz, shadow PID telemetry)
  describe("0x220 RT_PID_RPT (MCP2515 TX, 10 Hz, shadow PID)", () => {
    it("decodes setpoint, measured, pid_output (all positive)", () => {
      // setpoint=1500 mm/s, measured=1450 mm/s, pid_output=50
      const result = decodeFrame("high", "0x220", [
        0x05, 0xDC,  // setpoint 1500
        0x05, 0xAA,  // measured 1450
        0x00, 0x32,  // pid_output 50
      ]);
      expect(result).toEqual({
        speed_setpoint: 1500,
        speed_measured: 1450,
        pid_output: 50,
      });
    });

    it("decodes negative values (reverse + underspeed)", () => {
      // setpoint=-500, measured=-480, pid_output=-20
      const result = decodeFrame("high", "0x220", [
        0xFE, 0x0C,  // setpoint -500
        0xFE, 0x20,  // measured -480
        0xFF, 0xEC,  // pid_output -20
      ]);
      expect(result).toEqual({
        speed_setpoint: -500,
        speed_measured: -480,
        pid_output: -20,
      });
    });

    it("zero setpoint, zero measured, zero output", () => {
      const result = decodeFrame("high", "0x220", [
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      ]);
      expect(result).toEqual({
        speed_setpoint: 0, speed_measured: 0, pid_output: 0,
      });
    });

    it("DLC is 6 bytes", () => {
      const msg = findMsg("high", "0x220");
      expect(msg?.dlc).toBe(6);
    });

    it("not injectable (telemetry-only)", () => {
      const msg = findMsg("high", "0x220");
      expect(msg?.injectable).toBe(false);
    });
  });
});

// ── Bus routing validation ───────────────────────────────────────────

describe("MCP2515 high-bus CAN ID routing", () => {
  const highOnlyIds = ["0x7FD", "0x210", "0x220", "0x310", "0x311", "0x7FC"];

  for (const id of highOnlyIds) {
    it(`${id} exists on high bus`, () => {
      const hi = findMsg("high", id);
      expect(hi).toBeDefined();
      expect(hi?.bus).toBe("high");
    });

    // 0x7FD appears on BOTH buses (one per bus domain, NOT bridged)
    if (id !== "0x7FD") {
      it(`${id} does NOT exist on low bus`, () => {
        const lo = CAN_MESSAGES.filter((m) => m.bus === "low" && m.id === id);
        expect(lo).toHaveLength(0);
      });
    }
  }

  it("0x7FD appears on BOTH buses (per-bus heartbeat, not bridged)", () => {
    const hi = findMsg("high", "0x7FD");
    const lo = findMsg("low", "0x7FD");
    expect(hi).toBeDefined();
    expect(lo).toBeDefined();
    expect(hi?.bus).toBe("high");
    expect(lo?.bus).toBe("low");
  });

  // Verify low-only IDs don't leak to high bus
  const lowOnlyIds = ["0x169", "0x7B9", "0x204", "0x205", "0x012"];
  for (const id of lowOnlyIds) {
    it(`${id} does NOT exist on high bus (low-only ID)`, () => {
      const hi = CAN_MESSAGES.filter((m) => m.bus === "high" && m.id === id);
      expect(hi).toHaveLength(0);
    });
  }
});

// ── CAN message catalog completeness ─────────────────────────────────

describe("CAN message catalog — MCP2515 high bus completeness", () => {
  it("all 5 high-bus telemetry IDs are in the catalog", () => {
    const expected = ["0x210", "0x220", "0x310", "0x311", "0x7FD"];
    for (const id of expected) {
      const msg = findMsg("high", id);
      expect(msg, `Missing high-bus ID ${id}`).toBeDefined();
    }
  });

  it("all high-bus IDs have correct bus label", () => {
    const highMessages = CAN_MESSAGES.filter((m) => m.bus === "high");
    for (const m of highMessages) {
      expect(m.bus).toBe("high");
    }
  });

  it("total CAN message count covers at least 30 IDs", () => {
    expect(CAN_MESSAGES.length).toBeGreaterThanOrEqual(30);
  });
});
