// src/api/inject.ts

export interface InjectRequest {
  message_key: string;
  bus: 'high' | 'low';
  values: Record<string, any>;
}

const BASE = 'http://localhost:8000';

export async function injectCommand(request: InjectRequest): Promise<boolean> {
  try {
    const response = await fetch(`${BASE}/api/inject`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(request),
    });
    if (!response.ok) {
      console.error(`Inject failed: ${response.statusText}`);
      return false;
    }
    return true;
  } catch (err) {
    console.error('Network error during inject:', err);
    return false;
  }
}

// ── HOST commands ────────────────────────────────────────────────────────────

/** HOST_DRIVE_CMD — 0x300 — speed + yaw + gear */
export async function sendHostDriveCmd(
  speed_mmps: number,
  yaw_rate_mrad_s: number,
  gear: number // 0=N 1=D 2=S 3=R
) {
  return injectCommand({
    message_key: 'host:host_drive_cmd',
    bus: 'high',
    values: { speed_mmps, yaw_rate_mrad_s, gear },
  });
}

/** HOST_BRAKE_REQ — 0x301 — brake pressure (0–20000 kPa) */
export async function sendBrakeReq(brake_pressure_kpa: number) {
  return injectCommand({
    message_key: 'host:host_brake_req',
    bus: 'high',
    values: { brake_pressure_kpa },
  });
}

/** HOST_LIGHT_CMD — 0x302 — 4 boolean lights */
export async function sendLightCmd(
  left_turn: boolean,
  right_turn: boolean,
  brake_light: boolean,
  headlight: boolean
) {
  return injectCommand({
    message_key: 'host:host_light_cmd',
    bus: 'high',
    values: {
      left_turn: left_turn ? 1 : 0,
      right_turn: right_turn ? 1 : 0,
      brake_light: brake_light ? 1 : 0,
      headlight: headlight ? 1 : 0,
    },
  });
}

/** HOST_OBSTACLE_DIST — 0x400 — distance in mm (0xFFFFFFFF = clear) */
export async function sendObstacleDist(distance_mm: number) {
  return injectCommand({
    message_key: 'host:host_obstacle_dist',
    bus: 'high',
    values: { distance_mm },
  });
}

/** HOST_HEARTBEAT — 0x7FC — alive counter + health flags */
export async function sendHeartbeat(alive_ctr: number, health_flags: number) {
  return injectCommand({
    message_key: 'host:host_heartbeat',
    bus: 'high',
    values: { alive_ctr: alive_ctr & 0xff, health_flags: health_flags & 0xff },
  });
}

/** SAFETY_ESTOP — 0x001 — zero-length frame, triggers e-stop */
export async function sendEstop() {
  // Send on both buses
  await injectCommand({ message_key: 'safety:safety_estop', bus: 'high', values: {} });
  await injectCommand({ message_key: 'safety:safety_estop', bus: 'low', values: {} });
}

/** HMI_MODE_REQ — 0x111 — spoof HMI mode request */
export async function sendHmiModeReq(mode: 0 | 1, rolling_counter: number) {
  return injectCommand({
    message_key: 'hmi:hmi_mode_req',
    bus: 'high',
    values: { req_mode: mode, rolling_counter: rolling_counter & 0xff },
  });
}
