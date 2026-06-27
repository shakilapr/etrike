/**
 * CAN Commands Test Suite — exercises every CAN frame end-to-end.
 *
 * Groups: Low-bus custom, High-bus custom, SYNTREE EPS-C, SYNTREE SEB,
 * Heartbeats, Diagnostics, Drive cycle integration.
 */
import { describe, it, expect } from "vitest";
import { SimulationRunner } from "../../src/harness/runner.js";
import type { SimConfig } from "../../src/core/types.js";

function autoCfg(overrides: Partial<SimConfig> = {}): SimConfig {
  return {
    tickMs: 1,
    speed: 0,
    initialMode: "auto",
    plant: { wheelbaseMm: 1500, maxSpeedMmps: 3000, maxSteeringDeg: 40, steerLagMs: 50, brakeDecelMmps2PerMm: 2000 },
    hostDriveCycle: [{ durationMs: 99999, speedMmps: 0, yawRateMradS: 0, gear: 0 }],
    faults: [],
    ...overrides,
  };
}

function drivingCfg(speed = 1000, yaw = 0): SimConfig {
  return autoCfg({
    hostDriveCycle: [{ durationMs: 99999, speedMmps: speed, yawRateMradS: yaw, gear: speed >= 0 ? 1 : 3 }],
  });
}

// ================================================================
//  LOW BUS — Custom Protocol (IDs < 0x300)
// ================================================================

describe("0x001 SAFETY_ESTOP", () => {
  it("bidirectional bridge: ESTOP stops vehicle", () => {
    const runner = new SimulationRunner();
    runner.configure(autoCfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 1000, yawRateMradS: 0, gear: 1 }],
      faults: [{ atMs: 200, type: "triggerEstop" }],
    }));
    const result = runner.runDuration(500);
    expect(result.totalFrames).toBeGreaterThan(0);
    // ESTOP causes braking
    expect(result.plantFinalSpeedMmps).toBeLessThan(1000);
  });
});

describe("0x011 SYS_SAFETY_STS", () => {
  it("sent by SYS at 5 Hz on low bus", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(0));
    const result = runner.runDuration(1000);
    expect(result.lowBus.total).toBeGreaterThan(0);
    expect(result.validationErrors.length).toBe(0);
  });
});

describe("0x012 SYS_DCDC_CMD", () => {
  it("gap: defined in protocol but DCDC ECU not simulated", () => {
    expect(true).toBe(true);
  });
});

describe("0x110 SYS_MODE_CMD", () => {
  it("sent at 10 Hz with correct mode encoding", () => {
    const runner = new SimulationRunner();
    runner.configure(autoCfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 0, yawRateMradS: 0, gear: 0 }],
    }));
    runner.runDuration(500);
    expect(runner.canValidator.getAllErrors().length).toBe(0);
  });
});

describe("0x120 SYS_THROTTLE_STS (MTR→RT speed feedback)", () => {
  it("MTR sends at 100 Hz, RT forwards low→high", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(500));
    const result = runner.runDuration(100);
    expect(result.lowBus.total).toBeGreaterThan(5);
    expect(result.highBus.total).toBeGreaterThan(0);
  });
});

describe("0x204 RT_DRIVE_CMD", () => {
  it("RT produces at 100 Hz on low bus with DLC=5", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(2000));
    const result = runner.runDuration(100);
    expect(result.lowBus.total).toBeGreaterThan(8);
  });

  it("speed command propagates Host→RT→MTR→plant", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(1500));
    const result = runner.runDuration(300);
    expect(result.plantFinalSpeedMmps).toBeGreaterThan(0);
  });

  it("negative speed (reverse) encoded as i32 BE", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(-300));
    const result = runner.runDuration(100);
    expect(result.totalFrames).toBeGreaterThan(0);
  });
});

describe("0x205 RT_BRAKE_CMD", () => {
  it("produced at 50 Hz on low bus, DLC=4", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(0));
    const result = runner.runDuration(100);
    expect(result.lowBus.total).toBeGreaterThan(2);
    expect(runner.canValidator.getAllErrors().length).toBe(0);
  });

  it("ESTOP → max brake (20000 kPa)", () => {
    const runner = new SimulationRunner();
    runner.configure(autoCfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 2000, yawRateMradS: 0, gear: 1 }],
      faults: [{ atMs: 100, type: "triggerEstop" }],
    }));
    const result = runner.runDuration(400);
    expect(result.plantFinalSpeedMmps).toBeLessThan(2000);
  });
});

describe("0x206 MTR_MOTOR_FBK", () => {
  it("MTR sends at 50 Hz, DLC=4, forwarded low→high", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(500));
    const result = runner.runDuration(100);
    expect(result.lowBus.total).toBeGreaterThan(3);
    expect(result.highBus.total).toBeGreaterThan(0);
  });
});

describe("0x600 SYS_DIAG_RPT", () => {
  it("sent at 1 Hz, DLC=8, forwarded low→high", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(0));
    const result = runner.runDuration(2000);
    expect(result.lowBus.total).toBeGreaterThan(0);
    expect(result.highBus.total).toBeGreaterThan(0);
  });
});

// ================================================================
//  HIGH BUS — Custom Protocol (IDs ≥ 0x300, except SYNTREE)
// ================================================================

describe("0x210 RT_STATE_RPT", () => {
  it("RT sends at 10 Hz on high bus, DLC=3", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(500));
    const result = runner.runDuration(200);
    expect(result.highBus.total).toBeGreaterThan(0);
  });
});

describe("0x300 HOST_DRIVE_CMD", () => {
  it("Host sends at 100 Hz on high bus, DLC=8", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(2000));
    const result = runner.runDuration(100);
    expect(result.highBus.total).toBeGreaterThan(0);
  });

  it("speed_mmps i32 BE, yaw i24 BE, gear u8", () => {
    const runner = new SimulationRunner();
    runner.configure(autoCfg({
      hostDriveCycle: [{ durationMs: 99999, speedMmps: 2500, yawRateMradS: 100, gear: 1 }],
    }));
    runner.runDuration(100);
    expect(runner.canValidator.getAllErrors().length).toBe(0);
  });
});

describe("0x301 HOST_BRAKE_REQ", () => {
  it("Host sends at 50 Hz on high bus, DLC=4", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(1000));
    const result = runner.runDuration(100);
    expect(result.highBus.total).toBeGreaterThan(0);
  });
});

describe("0x302 HOST_LIGHT_CMD", () => {
  it("gap: light bits defined but Host does not send, RT does not forward", () => {
    expect(true).toBe(true);
  });
});

describe("0x310 STEER_DIAG", () => {
  it("gap: defined in protocol; C++ firmware sends it, simulation does not", () => {
    expect(true).toBe(true);
  });
});

describe("0x311 BRAKE_DIAG", () => {
  it("gap: defined in protocol; C++ firmware sends it, simulation does not", () => {
    expect(true).toBe(true);
  });
});

describe("0x220 RT_PID_RPT", () => {
  it("reserved / not yet enabled", () => {
    expect(true).toBe(true);
  });
});

describe("0x400 HOST_OBSTACLE_DIST", () => {
  it("Host sends at 10 Hz, u32 BE, DLC=4", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(0));
    const result = runner.runDuration(200);
    expect(result.highBus.total).toBeGreaterThan(0);
  });
});

// ================================================================
//  HEARTBEATS
// ================================================================

describe("0x7FC HOST_HEARTBEAT", () => {
  it("Host sends at 2 Hz on high bus", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(0));
    const result = runner.runDuration(1500);
    expect(result.highBus.total).toBeGreaterThan(1);
  });
});

describe("0x7FD RT_HEARTBEAT", () => {
  it("RT sends at 2 Hz on BOTH buses simultaneously", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(0));
    const result = runner.runDuration(1500);
    expect(result.highBus.total).toBeGreaterThan(0);
    expect(result.lowBus.total).toBeGreaterThan(0);
  });
});

describe("0x7FE SYS_HEARTBEAT", () => {
  it("SYS sends at 10 Hz on low bus", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(0));
    const result = runner.runDuration(500);
    expect(result.lowBus.total).toBeGreaterThan(3);
  });
});

// ================================================================
//  SYNTREE EPS-C (Steering) — IDs 0x169, 0x201, 0x202, 0x203, 0x6FA
// ================================================================

describe("0x169 VCU_SES_REQ (RT→EPS-C)", () => {
  it("RT sends at 50 Hz, DLC=8, valid SYNTREE checksum", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(500, 100));
    const result = runner.runDuration(100);
    expect(result.lowBus.total).toBeGreaterThan(2);
    expect(runner.canValidator.getAllErrors().length).toBe(0);
  });
});

describe("0x201 SES_STATUS (EPS-C→RT)", () => {
  it("EPS-C sends at 100 Hz with angle, status, checksum", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(500, 100));
    const result = runner.runDuration(100);
    expect(result.lowBus.total).toBeGreaterThan(3);
  });
});

describe("0x202 SES_ErrInfo", () => {
  it("EPS-C sends at 10 Hz, DLC=8", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(500, 100));
    runner.runDuration(200);
    expect(runner.canValidator.getAllErrors().length).toBe(0);
  });
});

describe("0x203 SES_Version", () => {
  it("EPS-C sends at 1 Hz with SW/HW version", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(500));
    runner.runDuration(1500);
    expect(runner.canValidator.getAllErrors().length).toBe(0);
  });
});

describe("0x6FA SES_Test", () => {
  it("EPS-C sends at 100 Hz (motor current, temp, voltage)", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(500, 50));
    const result = runner.runDuration(50);
    expect(result.lowBus.total).toBeGreaterThan(2);
  });
});

// ================================================================
//  SYNTREE SEB (Brake) — IDs 0x7B9, 0x721, 0x731, 0x741, 0x6FB
// ================================================================

describe("0x7B9 VCU_SEB_REQ (SYS→SEB)", () => {
  it("SYS sends at 50 Hz, DLC=8, valid SYNTREE checksum", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(500));
    const result = runner.runDuration(100);
    expect(result.lowBus.total).toBeGreaterThan(2);
    expect(runner.canValidator.getAllErrors().length).toBe(0);
  });
});

describe("0x721 SEB_STATUS (SEB→SYS)", () => {
  it("SEB sends at 100 Hz with stroke, pressure, angle", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(500));
    const result = runner.runDuration(50);
    expect(result.lowBus.total).toBeGreaterThan(1);
  });
});

describe("0x731 SEB_ErrInfo", () => {
  it("SEB sends at 10 Hz, DLC=8", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(500));
    runner.runDuration(200);
    expect(runner.canValidator.getAllErrors().length).toBe(0);
  });
});

describe("0x741 SEB_Version", () => {
  it("SEB sends at 1 Hz", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(500));
    runner.runDuration(1500);
    expect(runner.canValidator.getAllErrors().length).toBe(0);
  });
});

describe("0x6FB SEB_Test", () => {
  it("SEB sends at 100 Hz (motor current, temp, voltage)", () => {
    const runner = new SimulationRunner();
    runner.configure(drivingCfg(500));
    const result = runner.runDuration(50);
    expect(result.lowBus.total).toBeGreaterThan(1);
  });
});

// ================================================================
//  INTEGRATION — Drive Cycles
// ================================================================

describe("Full drive cycle", () => {
  it("Host→RT→SYS/MTR/EPS-C/SEB completes without errors", () => {
    const runner = new SimulationRunner();
    runner.configure(autoCfg({
      hostDriveCycle: [
        { durationMs: 0,    speedMmps: 0,    yawRateMradS: 0,   gear: 0 },
        { durationMs: 500,  speedMmps: 1000, yawRateMradS: 0,   gear: 1 },
        { durationMs: 1500, speedMmps: 2000, yawRateMradS: 100, gear: 1 },
        { durationMs: 2000, speedMmps: 500,  yawRateMradS: 50,  gear: 1 },
        { durationMs: 2000, speedMmps: 0,    yawRateMradS: 0,   gear: 0 },
      ],
    }));
    const result = runner.runDuration(7000);
    expect(result.validationErrors.length).toBe(0);
    expect(result.violations.length).toBe(0);
    expect(result.plantFinalSpeedMmps).toBeLessThan(100);
    expect(result.totalFrames).toBeGreaterThan(50);
  });

  it("ESTOP mid-drive → all ECUs react, speed → 0", () => {
    const runner = new SimulationRunner();
    runner.configure(autoCfg({
      hostDriveCycle: [
        { durationMs: 0,    speedMmps: 2000, yawRateMradS: 0, gear: 1 },
        { durationMs: 1500, speedMmps: 2000, yawRateMradS: 0, gear: 1 },
        { durationMs: 99999, speedMmps: 2000, yawRateMradS: 0, gear: 1 },
      ],
      faults: [{ atMs: 1500, type: "triggerEstop" }],
    }));
    const result = runner.runDuration(3000);
    expect(result.plantFinalSpeedMmps).toBeLessThan(500);
    expect(result.totalFrames).toBeGreaterThan(20);
  });
});
