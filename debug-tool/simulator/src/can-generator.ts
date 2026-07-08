// CAN frame generator — produces realistic synthetic CAN traffic
// Message definitions match shared/can/can_signals.yaml

import type { CanFrame, Bus } from "@etrike/debug-shared";

export interface ProfileEntry {
  bus: Bus;
  id: string;
  name: string;
  interval_ms: number; // how often to generate this frame
  dlc: number;
}

export type Profile = ProfileEntry[];

// Default profile from architecture §8
export const DEFAULT_PROFILE: Profile = [
  // ── High bus (Jetson ↔ RT) ──
  { bus: "high", id: "0x001", name: "ESTOP_CMD", interval_ms: 100, dlc: 0 },
  { bus: "high", id: "0x011", name: "SYS_SAFETY_STS", interval_ms: 100, dlc: 3 },
  { bus: "high", id: "0x120", name: "HOST_SPEED_OPS", interval_ms: 100, dlc: 2 },
  { bus: "high", id: "0x206", name: "MTR_MOTOR_FBK", interval_ms: 50, dlc: 4 },
  { bus: "high", id: "0x210", name: "RT_STATE", interval_ms: 100, dlc: 6 },
  { bus: "high", id: "0x220", name: "RT_LED_STATUS", interval_ms: 200, dlc: 6 },
  { bus: "high", id: "0x300", name: "HOST_DRIVE_CMD", interval_ms: 100, dlc: 8 },
  { bus: "high", id: "0x301", name: "RT_BRAKE_PRES_CMD", interval_ms: 100, dlc: 4 },
  { bus: "high", id: "0x302", name: "RT_BRAKE_PRES_FBK", interval_ms: 100, dlc: 1 },
  { bus: "high", id: "0x400", name: "OBSTACLE", interval_ms: 100, dlc: 4 },
  { bus: "high", id: "0x600", name: "DIAGNOSTIC", interval_ms: 1000, dlc: 8 },
  { bus: "high", id: "0x7FC", name: "JETSON_HEARTBEAT", interval_ms: 500, dlc: 2 },
  { bus: "high", id: "0x7FD", name: "RT_HEARTBEAT", interval_ms: 500, dlc: 2 },

  // ── Low bus (RT ↔ actuators) ──
  { bus: "low", id: "0x001", name: "ESTOP_CMD", interval_ms: 100, dlc: 0 },
  { bus: "low", id: "0x011", name: "SYS_SAFETY_STS", interval_ms: 100, dlc: 3 },
  { bus: "low", id: "0x012", name: "SYS_CTRL_CMD", interval_ms: 100, dlc: 1 },
  { bus: "low", id: "0x110", name: "SYS_MODE_CMD", interval_ms: 0, dlc: 1 },
  { bus: "low", id: "0x120", name: "HOST_SPEED_OPS", interval_ms: 50, dlc: 2 },
  { bus: "low", id: "0x169", name: "VCU_STEER_CMD", interval_ms: 50, dlc: 8 },
  { bus: "low", id: "0x201", name: "SES_STEER_STATUS", interval_ms: 50, dlc: 8 },
  { bus: "low", id: "0x202", name: "SES_SENSOR_STATUS", interval_ms: 100, dlc: 8 },
  { bus: "low", id: "0x203", name: "SES_RES_CMD", interval_ms: 200, dlc: 8 },
  { bus: "low", id: "0x204", name: "RT_DRIVE_CMD", interval_ms: 50, dlc: 5 },
  { bus: "low", id: "0x205", name: "RT_BRAKE_TARGET", interval_ms: 100, dlc: 4 },
  { bus: "low", id: "0x206", name: "VDDP_STATUS", interval_ms: 100, dlc: 4 },
  { bus: "low", id: "0x302", name: "RT_BRAKE_PRES_FBK", interval_ms: 100, dlc: 1 },
  { bus: "low", id: "0x600", name: "DIAGNOSTIC", interval_ms: 1000, dlc: 8 },
  { bus: "low", id: "0x6FA", name: "BMS_SOC_SOH", interval_ms: 1000, dlc: 8 },
  { bus: "low", id: "0x6FB", name: "BMS_TEMP", interval_ms: 1000, dlc: 8 },
  { bus: "low", id: "0x721", name: "SEB_PRES_FBK", interval_ms: 100, dlc: 8 },
  { bus: "low", id: "0x731", name: "SEB_DIAG", interval_ms: 1000, dlc: 8 },
  { bus: "low", id: "0x741", name: "SEB_SERVICE", interval_ms: 2000, dlc: 8 },
  { bus: "low", id: "0x7B9", name: "VCU_BRAKE_CMD", interval_ms: 100, dlc: 8 },
  { bus: "low", id: "0x7FD", name: "RT_HEARTBEAT", interval_ms: 500, dlc: 2 },
  { bus: "low", id: "0x7FE", name: "EPS_HEARTBEAT", interval_ms: 500, dlc: 2 },
];

// Generate varying data for each CAN ID
const counters = new Map<string, number>();

function counter(id: string, max = 255): number {
  const c = (counters.get(id) ?? 0) + 1;
  counters.set(id, c > max ? 0 : c);
  return c;
}

// Produce a sine-wave speed value for drive commands
let simTime = 0;
export function tickSimTime(dtMs: number): void {
  simTime += dtMs / 1000;
}

function sineSpeed(): number {
  // Oscillate between 0 and 2000 mm/s
  return Math.round(1000 + 1000 * Math.sin(simTime * 0.5));
}

function yawRate(): number {
  return Math.round(500 * Math.sin(simTime * 0.3));
}

export function generateFrame(entry: ProfileEntry): CanFrame {
  const id = entry.id;
  const now = Date.now() / 1000;
  const data = new Array(entry.dlc).fill(0);

  switch (id) {
    case "0x300": { // HOST_DRIVE_CMD
      const speed = sineSpeed();
      const yaw = yawRate();
      data[0] = (speed >> 8) & 0xFF;
      data[1] = speed & 0xFF;
      data[2] = (yaw >> 8) & 0xFF;
      data[3] = yaw & 0xFF;
      data[4] = 1; // gear = D
      data[5] = counter(id);
      break;
    }
    case "0x204": { // RT_DRIVE_CMD
      const speed = sineSpeed() + Math.round(Math.random() * 50 - 25);
      data[0] = (speed >> 8) & 0xFF;
      data[1] = speed & 0xFF;
      data[2] = 0;
      data[3] = 1; // gear
      data[4] = counter(id);
      break;
    }
    case "0x201": { // SES_STEER_STATUS
      const angle = Math.round(300 * Math.sin(simTime * 0.4));
      data[0] = (angle >> 8) & 0xFF;
      data[1] = angle & 0xFF;
      data[2] = counter(id);
      break;
    }
    case "0x206": { // MTR_MOTOR_FBK / VDDP_STATUS
      const rpm = Math.round(2000 + 500 * Math.sin(simTime * 0.6));
      data[0] = (rpm >> 8) & 0xFF;
      data[1] = rpm & 0xFF;
      data[2] = counter(id);
      break;
    }
    case "0x7FC": // JETSON_HEARTBEAT
    case "0x7FD": // RT_HEARTBEAT
    case "0x7FE": { // EPS_HEARTBEAT
      data[0] = counter(id);
      data[1] = 0x01; // health OK
      break;
    }
    case "0x011": { // SYS_SAFETY_STS
      data[0] = 0x00; // estop not active
      data[1] = counter(id);
      break;
    }
    case "0x600": { // DIAGNOSTIC
      data[0] = counter(id);
      data[1] = 0; // no faults
      break;
    }
    case "0x721": { // SEB_PRES_FBK
      const pressure = Math.round(50 + 30 * Math.sin(simTime * 0.5));
      data[0] = pressure;
      data[1] = counter(id);
      break;
    }
    default: {
      // Generic: fill with rolling counter
      data[0] = counter(id);
      for (let i = 1; i < entry.dlc; i++) {
        data[i] = (data[0] + i) & 0xFF;
      }
      break;
    }
  }

  return {
    ts: now,
    bus: entry.bus,
    id: entry.id,
    name: entry.name,
    dlc: entry.dlc,
    data: data.slice(0, entry.dlc),
    decoded: { sim_generated: true },
  };
}

export function generateStats(busStats: Map<string, { count: number; active: boolean }>) {
  const now = Date.now() / 1000;
  const buses: Record<string, unknown> = {};
  for (const [bus, s] of busStats) {
    buses[bus] = {
      active: s.active,
      total: s.count,
      fps: 0, // calculated by sim engine
      load_pct: 0,
      tec: 0,
      rec: 0,
      by_id: {},
    };
  }
  return { type: "stats", ts: now, buses };
}
