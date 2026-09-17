/**
 * Canonical E-Trike CAN Protocol Diagnostics & Refusal Dictionary
 *
 * Sourced directly from official protocol contracts and specifications:
 * - protocol/contracts/sys.yaml, rt.yaml, mtr.yaml, ses.yaml, seb.yaml, host.yaml, hmi.yaml
 * - protocol/diagnostics/diagnostics.yaml (Schema v2)
 * - protocol/generated/cpp/diagnostics.hpp
 * - sys-esp32/src/inhibit_state.h & mode_manager.h
 * - rt-esp32/src/config.h & safety_monitor.h
 */

import type { MessageState } from '../store'
import { signalNum, signalText } from './signals'

export type ProtocolSeverity = 'INFO' | 'WARNING' | 'ERROR' | 'CRITICAL'
export type ProtocolReaction = 'NONE' | 'WARN' | 'DERATE' | 'INHIBIT' | 'CSTOP' | 'ESTOP'
export type ProtocolSubsystem = 'SAFETY' | 'COMMUNICATION' | 'STEERING' | 'BRAKE' | 'POWERTRAIN' | 'SUPERVISION' | 'INTERFACE' | 'DIAGNOSTIC' | 'CONTROL' | 'SECURITY'

export type CanAuditCategory = 'refusal' | 'state' | 'report' | 'fault'

export type CanAuditEntry = {
  id: string
  timestamp: string
  category: CanAuditCategory
  canIdHex: string
  msgName: string
  subsystem: 'Steering' | 'Braking' | 'Speed' | 'System' | 'Safety' | 'Communication' | 'Powertrain'
  title: string
  description: string
  severity?: 'INFO' | 'WARNING' | 'ERROR' | 'CRITICAL'
  reaction?: 'NONE' | 'WARN' | 'DERATE' | 'INHIBIT' | 'CSTOP' | 'ESTOP'
  isRootEstop?: boolean
  details?: Record<string, unknown>
}

export type ProtocolDiagnosticDef = {
  id: number
  idHex: string
  key: string
  reporter: 'SYS' | 'RT' | 'MTR' | 'SES' | 'SEB' | 'HOST'
  source: string
  subsystem: ProtocolSubsystem
  severity: ProtocolSeverity
  reaction: ProtocolReaction
  latching: boolean
  description: string
  refusalType?: 'reset_blocker' | 'steering_inhibit' | 'traction_inhibit' | 'mode_refusal' | 'actuator_blocker'
}

/** Official Canonical Diagnostic Codes from protocol/diagnostics/diagnostics.yaml & protocol/generated/cpp/diagnostics.hpp */
export const PROTOCOL_DIAGNOSTICS_CATALOG: Record<number, ProtocolDiagnosticDef> = {
  // ── SYS Diagnostics (Namespace 0x01) ──
  0x0101: {
    id: 0x0101,
    idHex: '0x0101',
    key: 'SYS_ESTOP_BUTTON_ASSERTED',
    reporter: 'SYS',
    source: 'ESTOP_BUTTON',
    subsystem: 'SAFETY',
    severity: 'CRITICAL',
    reaction: 'ESTOP',
    latching: true,
    description: 'Hardware emergency stop button depressed (GPIO active low). Emergency stop latched across vehicle.',
    refusalType: 'reset_blocker',
  },
  0x0102: {
    id: 0x0102,
    idHex: '0x0102',
    key: 'SYS_RT_HEARTBEAT_TIMEOUT',
    reporter: 'SYS',
    source: 'RT',
    subsystem: 'COMMUNICATION',
    severity: 'CRITICAL',
    reaction: 'ESTOP',
    latching: true,
    description: 'RT supervisor heartbeat (0x7FD) silence exceeded watchdog timeout (>500ms). Safe shutdown engaged.',
    refusalType: 'reset_blocker',
  },
  0x0103: {
    id: 0x0103,
    idHex: '0x0103',
    key: 'SYS_SEB_STATUS_TIMEOUT',
    reporter: 'SYS',
    source: 'SEB',
    subsystem: 'COMMUNICATION',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'Smart Electronic Brake status (0x721) timeout (>50ms). Drive traction inhibited until brake telemetry recovers.',
    refusalType: 'actuator_blocker',
  },
  0x0104: {
    id: 0x0104,
    idHex: '0x0104',
    key: 'SYS_SEB_L3_FAULT',
    reporter: 'SYS',
    source: 'SEB',
    subsystem: 'BRAKE',
    severity: 'CRITICAL',
    reaction: 'ESTOP',
    latching: true,
    description: 'SEB reported Level-3 internal transducer/actuator fault (error_status=3). Active blocker asserts kLatchedSebL3; resets prohibited.',
    refusalType: 'reset_blocker',
  },
  0x0105: {
    id: 0x0105,
    idHex: '0x0105',
    key: 'SYS_EGAS_L2_MISMATCH',
    reporter: 'SYS',
    source: 'MTR',
    subsystem: 'POWERTRAIN',
    severity: 'CRITICAL',
    reaction: 'ESTOP',
    latching: true,
    description: 'EGAS Level 2 safety plausibility mismatch: Motor command speed (0x204) differs from motor echo (0x206) > 250 mm/s.',
    refusalType: 'reset_blocker',
  },
  0x0106: {
    id: 0x0106,
    idHex: '0x0106',
    key: 'SYS_MTR_FBK_TIMEOUT',
    reporter: 'SYS',
    source: 'MTR',
    subsystem: 'COMMUNICATION',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'Motor inverter feedback (0x206) timeout. Traction inhibited by SYS (kInhibitMtrFbkLoss).',
    refusalType: 'traction_inhibit',
  },
  0x0107: {
    id: 0x0107,
    idHex: '0x0107',
    key: 'SYS_SEB_BRAKE_FOLLOW_ERR',
    reporter: 'SYS',
    source: 'SEB',
    subsystem: 'BRAKE',
    severity: 'WARNING',
    reaction: 'WARN',
    latching: false,
    description: 'Mechanical caliper stroke tracking error: Caliper stroke delta exceeds commanded travel threshold.',
    refusalType: 'actuator_blocker',
  },
  0x0108: {
    id: 0x0108,
    idHex: '0x0108',
    key: 'SYS_MTR_ESTOP_ACK_TIMEOUT',
    reporter: 'SYS',
    source: 'MTR',
    subsystem: 'SAFETY',
    severity: 'CRITICAL',
    reaction: 'ESTOP',
    latching: true,
    description: 'Motor inverter failed to acknowledge ESTOP de-energization within deadline. Contactor power isolated.',
    refusalType: 'reset_blocker',
  },
  0x0109: {
    id: 0x0109,
    idHex: '0x0109',
    key: 'SYS_CAN_BUS_OFF',
    reporter: 'SYS',
    source: 'CAN',
    subsystem: 'INTERFACE',
    severity: 'CRITICAL',
    reaction: 'ESTOP',
    latching: true,
    description: 'Low CAN bus (ESP32 TWAI) entered Bus-Off state (TEC > 255). CAN communication collapsed.',
    refusalType: 'reset_blocker',
  },
  0x010A: {
    id: 0x010A,
    idHex: '0x010A',
    key: 'SYS_TASK_DEADLINE_MISSED',
    reporter: 'SYS',
    source: 'SYS',
    subsystem: 'DIAGNOSTIC',
    severity: 'WARNING',
    reaction: 'WARN',
    latching: false,
    description: 'FreeRTOS safety or dispatch cyclic task deadline missed.',
  },
  0x010B: {
    id: 0x010B,
    idHex: '0x010B',
    key: 'SYS_GEAR_MISMATCH',
    reporter: 'SYS',
    source: 'MTR',
    subsystem: 'POWERTRAIN',
    severity: 'WARNING',
    reaction: 'WARN',
    latching: false,
    description: 'Inverter feedback gear state does not match commanded transmission gear.',
  },
  0x010C: {
    id: 0x010C,
    idHex: '0x010C',
    key: 'SYS_SEB_TEMP_HIGH',
    reporter: 'SYS',
    source: 'SEB',
    subsystem: 'BRAKE',
    severity: 'WARNING',
    reaction: 'WARN',
    latching: false,
    description: 'SEB actuator controller thermal sensor exceeded threshold.',
  },
  0x010D: {
    id: 0x010D,
    idHex: '0x010D',
    key: 'SYS_CAN_RX_OVERFLOW',
    reporter: 'SYS',
    source: 'CAN',
    subsystem: 'INTERFACE',
    severity: 'WARNING',
    reaction: 'WARN',
    latching: false,
    description: 'Low CAN TWAI receive FIFO queue overflowed.',
  },
  0x010E: {
    id: 0x010E,
    idHex: '0x010E',
    key: 'SYS_RT_SETPOINT_STALE',
    reporter: 'SYS',
    source: 'RT',
    subsystem: 'CONTROL',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'RT motion control setpoint stream became stale on Low CAN.',
    refusalType: 'traction_inhibit',
  },
  0x0124: {
    id: 0x0124,
    idHex: '0x0124',
    key: 'SYS_BRAKE_THROTTLE_CONFLICT',
    reporter: 'SYS',
    source: 'VEHICLE_CONTROL',
    subsystem: 'SUPERVISION',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'Brake-throttle overlap conflict: Throttle command requested while brake hydraulic clamping pressure > 100 kPa.',
    refusalType: 'traction_inhibit',
  },

  // ── RT Diagnostics (Namespace 0x02) ──
  0x0201: {
    id: 0x0201,
    idHex: '0x0201',
    key: 'RT_HOST_HEARTBEAT_TIMEOUT',
    reporter: 'RT',
    source: 'HOST',
    subsystem: 'COMMUNICATION',
    severity: 'CRITICAL',
    reaction: 'ESTOP',
    latching: true,
    description: 'Host autonomy guidance heartbeat (0x7FC) timeout (>500ms). Trajectory watchdog triggered.',
    refusalType: 'reset_blocker',
  },
  0x0202: {
    id: 0x0202,
    idHex: '0x0202',
    key: 'RT_SYS_HEARTBEAT_TIMEOUT',
    reporter: 'RT',
    source: 'SYS',
    subsystem: 'COMMUNICATION',
    severity: 'CRITICAL',
    reaction: 'ESTOP',
    latching: true,
    description: 'SYS management heartbeat (0x7FE) timeout. RT lost authority supervisor sync.',
    refusalType: 'reset_blocker',
  },
  0x0203: {
    id: 0x0203,
    idHex: '0x0203',
    key: 'RT_STEER_FOLLOWING_ERROR',
    reporter: 'RT',
    source: 'SES',
    subsystem: 'STEERING',
    severity: 'CRITICAL',
    reaction: 'ESTOP',
    latching: true,
    description: 'Steering tracking excursion: Discrepancy between RT target (0x169) and SES actual angle (0x201) exceeded tolerance (>10°).',
    refusalType: 'steering_inhibit',
  },
  0x0204: {
    id: 0x0204,
    idHex: '0x0204',
    key: 'RT_CAN_BUS_OFF',
    reporter: 'RT',
    source: 'CAN',
    subsystem: 'INTERFACE',
    severity: 'CRITICAL',
    reaction: 'ESTOP',
    latching: true,
    description: 'RT Low CAN controller entered Bus-Off state.',
    refusalType: 'reset_blocker',
  },
  0x0205: {
    id: 0x0205,
    idHex: '0x0205',
    key: 'RT_CAN_HIGH_BUS_OFF',
    reporter: 'RT',
    source: 'CAN',
    subsystem: 'INTERFACE',
    severity: 'CRITICAL',
    reaction: 'ESTOP',
    latching: true,
    description: 'RT High CAN controller (MCP2515 SPI) entered Bus-Off state.',
    refusalType: 'reset_blocker',
  },
  0x0206: {
    id: 0x0206,
    idHex: '0x0206',
    key: 'RT_SES_L3_FAULT',
    reporter: 'RT',
    source: 'SES',
    subsystem: 'STEERING',
    severity: 'CRITICAL',
    reaction: 'ESTOP',
    latching: true,
    description: 'SES Steer-by-Wire controller reported L3 critical internal hardware failure (0x201).',
    refusalType: 'steering_inhibit',
  },
  0x0208: {
    id: 0x0208,
    idHex: '0x0208',
    key: 'RT_HOST_DRIVE_CMD_STALE',
    reporter: 'RT',
    source: 'HOST',
    subsystem: 'COMMUNICATION',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'HOST_DRIVE_CMD (0x300) stream stalled (>200ms). RT motion watchdog clamped propulsion speed to 0.',
    refusalType: 'traction_inhibit',
  },
  0x0209: {
    id: 0x0209,
    idHex: '0x0209',
    key: 'RT_STEER_SYNC_TIMEOUT',
    reporter: 'RT',
    source: 'SES',
    subsystem: 'STEERING',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'SES status (0x201) frame silence exceeded timeout (>50ms). Steering guidance inhibited.',
    refusalType: 'steering_inhibit',
  },
  0x020A: {
    id: 0x020A,
    idHex: '0x020A',
    key: 'RT_STEER_IMPLAUSIBLE_ANGLE',
    reporter: 'RT',
    source: 'SES',
    subsystem: 'STEERING',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'SES measured angle outside physical mechanical boundaries (> ±90°).',
    refusalType: 'steering_inhibit',
  },
  0x020B: {
    id: 0x020B,
    idHex: '0x020B',
    key: 'RT_STEER_ESTOP_JAM',
    reporter: 'RT',
    source: 'SES',
    subsystem: 'STEERING',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'Mechanical steering lock/jam detected: Maximum current applied without angular motion.',
    refusalType: 'steering_inhibit',
  },
  0x020C: {
    id: 0x020C,
    idHex: '0x020C',
    key: 'RT_SYS_SAFETY_STS_LOSS',
    reporter: 'RT',
    source: 'SYS',
    subsystem: 'SAFETY',
    severity: 'CRITICAL',
    reaction: 'ESTOP',
    latching: true,
    description: 'SYS_SAFETY_STS (0x011) frame silence on Low CAN. RT supervisor latched emergency stop.',
    refusalType: 'reset_blocker',
  },
  0x020D: {
    id: 0x020D,
    idHex: '0x020D',
    key: 'RT_LOW_CAN_PEER_TIMEOUT',
    reporter: 'RT',
    source: 'CAN',
    subsystem: 'COMMUNICATION',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'RT detected peer communication timeout on Low CAN bus.',
  },
  0x020F: {
    id: 0x020F,
    idHex: '0x020F',
    key: 'RT_TASK_HEALTH_FAULT',
    reporter: 'RT',
    source: 'RT',
    subsystem: 'SUPERVISION',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'RT internal cyclic task health check failed.',
  },
  0x0210: {
    id: 0x0210,
    idHex: '0x0210',
    key: 'RT_SES_TELEMETRY_WARN',
    reporter: 'RT',
    source: 'SES',
    subsystem: 'STEERING',
    severity: 'WARNING',
    reaction: 'WARN',
    latching: false,
    description: 'SES steer controller telemetry quality degraded or experiencing minor frame drops.',
  },
  0x0211: {
    id: 0x0211,
    idHex: '0x0211',
    key: 'RT_CAN_HIGH_RX_OVERFLOW',
    reporter: 'RT',
    source: 'CAN',
    subsystem: 'INTERFACE',
    severity: 'WARNING',
    reaction: 'WARN',
    latching: false,
    description: 'High CAN MCP2515 receive FIFO overflow occurred.',
  },
  0x0212: {
    id: 0x0212,
    idHex: '0x0212',
    key: 'RT_DIRECT_STEER_STALE',
    reporter: 'RT',
    source: 'HOST',
    subsystem: 'STEERING',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'HOST_STEER_CMD (0x303) stream became stale during autonomous steering guidance.',
    refusalType: 'steering_inhibit',
  },
  0x0213: {
    id: 0x0213,
    idHex: '0x0213',
    key: 'RT_MTR_FBK_TIMEOUT',
    reporter: 'RT',
    source: 'MTR',
    subsystem: 'COMMUNICATION',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'Motor inverter telemetry (0x206) absent > 100ms. Closed-loop speed tracking inhibited.',
    refusalType: 'traction_inhibit',
  },
  0x0229: {
    id: 0x0229,
    idHex: '0x0229',
    key: 'RT_PLANNER_CONTRADICTION',
    reporter: 'RT',
    source: 'HOST',
    subsystem: 'SUPERVISION',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'Planner contradiction: Host commanded high throttle (>1 m/s) and heavy braking (>2000 kPa) simultaneously.',
    refusalType: 'mode_refusal',
  },
  0x022A: {
    id: 0x022A,
    idHex: '0x022A',
    key: 'RT_PLANNER_OBSTACLE_CONFLICT',
    reporter: 'RT',
    source: 'HOST',
    subsystem: 'SUPERVISION',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'Obstacle guard refusal: Drive commanded forward while perception obstacle distance (0x400) is under critical limit (<300 mm).',
    refusalType: 'traction_inhibit',
  },
  0x0237: {
    id: 0x0237,
    idHex: '0x0237',
    key: 'RT_DYNAMIC_CLAMP_EXCEEDED',
    reporter: 'RT',
    source: 'HOST',
    subsystem: 'SUPERVISION',
    severity: 'ERROR',
    reaction: 'DERATE',
    latching: false,
    description: 'Speed-dependent steer angle clamping: Requested steer angle exceeds maximum safe curvature limit at current velocity.',
    refusalType: 'steering_inhibit',
  },

  // ── MTR Diagnostics (Namespace 0x03) ──
  0x0301: {
    id: 0x0301,
    idHex: '0x0301',
    key: 'MTR_RT_DRIVE_CMD_TIMEOUT',
    reporter: 'MTR',
    source: 'RT',
    subsystem: 'COMMUNICATION',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'RT drive command stream (0x204) timeout. Motor inverter inhibited propulsion torque.',
    refusalType: 'traction_inhibit',
  },
  0x0305: {
    id: 0x0305,
    idHex: '0x0305',
    key: 'MTR_SYS_SAFETY_STS_TIMEOUT',
    reporter: 'MTR',
    source: 'SYS',
    subsystem: 'SAFETY',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'SYS_SAFETY_STS (0x011) timeout observed by MTR. Propulsion inhibited.',
  },
  0x0306: {
    id: 0x0306,
    idHex: '0x0306',
    key: 'MTR_SYS_SAFETY_CRC_ERROR',
    reporter: 'MTR',
    source: 'SYS',
    subsystem: 'SAFETY',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'E2E CRC checksum failure on SYS_SAFETY_STS frame (0x011). Frame discarded.',
  },
  0x0307: {
    id: 0x0307,
    idHex: '0x0307',
    key: 'MTR_SYS_SAFETY_COUNTER_STALE',
    reporter: 'MTR',
    source: 'SYS',
    subsystem: 'SAFETY',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'Rolling counter frozen on SYS_SAFETY_STS stream.',
  },
  0x0308: {
    id: 0x0308,
    idHex: '0x0308',
    key: 'MTR_SYS_MODE_CMD_TIMEOUT',
    reporter: 'MTR',
    source: 'SYS',
    subsystem: 'CONTROL',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'SYS_MODE_CMD (0x110) stream silence exceeded timeout threshold.',
  },
  0x0309: {
    id: 0x0309,
    idHex: '0x0309',
    key: 'MTR_SYS_PWR_CMD_TIMEOUT',
    reporter: 'MTR',
    source: 'SYS',
    subsystem: 'CONTROL',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'SYS_PWR_CMD (0x113) stream silence exceeded timeout threshold.',
  },
  0x030A: {
    id: 0x030A,
    idHex: '0x030A',
    key: 'MTR_REARM_SEQUENCE_VIOLATION',
    reporter: 'MTR',
    source: 'MTR',
    subsystem: 'SAFETY',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'Inverter rearm attempted before contactor was precharged or safe state confirmed.',
    refusalType: 'mode_refusal',
  },
  0x030D: {
    id: 0x030D,
    idHex: '0x030D',
    key: 'MTR_FDCAN_BUS_OFF',
    reporter: 'MTR',
    source: 'CAN',
    subsystem: 'INTERFACE',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'Motor STM32 FDCAN controller entered Bus-Off condition.',
  },
  0x030E: {
    id: 0x030E,
    idHex: '0x030E',
    key: 'MTR_FDCAN_RX_OVERFLOW',
    reporter: 'MTR',
    source: 'CAN',
    subsystem: 'INTERFACE',
    severity: 'WARNING',
    reaction: 'WARN',
    latching: false,
    description: 'Motor STM32 FDCAN RX FIFO overflow occurred.',
  },
  0x030F: {
    id: 0x030F,
    idHex: '0x030F',
    key: 'MTR_SPEED_SETPOINT_INVALID',
    reporter: 'MTR',
    source: 'RT',
    subsystem: 'CONTROL',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'Speed setpoint commanded outside valid motor envelope limits.',
    refusalType: 'traction_inhibit',
  },
  0x0311: {
    id: 0x0311,
    idHex: '0x0311',
    key: 'MTR_CMD_STREAM_UNAUTHORISED',
    reporter: 'MTR',
    source: 'MTR',
    subsystem: 'SECURITY',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'Motor drive command received from unauthorized sender or incorrect bus.',
  },
  0x0314: {
    id: 0x0314,
    idHex: '0x0314',
    key: 'MTR_WATCHDOG_RESET',
    reporter: 'MTR',
    source: 'MTR',
    subsystem: 'DIAGNOSTIC',
    severity: 'ERROR',
    reaction: 'INHIBIT',
    latching: false,
    description: 'Motor STM32 Independent Watchdog (IWDG) triggered hardware MCU reset.',
  },
}

/**
 * Real CAN Protocol Blocker Mask definitions from sys-esp32/src/inhibit_state.h
 * Transmitted in 0x115 (SYS_ESTOP_RESET_RSP) blocker_mask field and 0x500/0x501 (NODE_STATUS) block_mask.
 */
export const SYS_ESTOP_RESET_BLOCKERS: Record<number, { name: string; description: string }> = {
  0x0001: {
    name: 'PHYSICAL_ESTOP',
    description: 'Hardware emergency stop button is physically depressed or interlock open (GPIO active low).',
  },
  0x0002: {
    name: 'LATCHED_FAULT_ASSERTED',
    description: 'Underlying cause of latched safety fault (SEB L3 error, brake following failure) remains asserted.',
  },
  0x0004: {
    name: 'VEHICLE_MOVING',
    description: 'Vehicle wheel speed exceeds safe stationary threshold (>50 mm/s). Reset prohibited while moving.',
  },
  0x0008: {
    name: 'HEARTBEAT_LOSS',
    description: 'Safety-critical ECU heartbeat (SYS 0x7FE or RT 0x7FD) is lost or corrupted.',
  },
  0x0010: {
    name: 'MTR_ESTOP_UNACKED',
    description: 'Propulsion motor inverter has not acknowledged emergency stop de-energization.',
  },
  0x0020: {
    name: 'TRANSIENT_INHIBIT',
    description: 'Active transient inhibit asserted (MTR feedback loss, SEB comms timeout, or brake following excursion).',
  },
  0x0040: {
    name: 'INVALID_TOKEN',
    description: 'Remote reset request token was invalid (must match kRemoteResetTokenMagic 0xA55A).',
  },
}

/** Decodes a 16-bit blocker_mask into individual canonical protocol blockers */
export function decodeBlockerMask(mask: number): Array<{ bit: number; name: string; description: string }> {
  const result: Array<{ bit: number; name: string; description: string }> = []
  for (let bit = 0; bit < 16; bit++) {
    const flag = 1 << bit
    if ((mask & flag) !== 0) {
      const def = SYS_ESTOP_RESET_BLOCKERS[flag]
      if (def) {
        result.push({ bit, name: def.name, description: def.description })
      } else {
        result.push({ bit, name: `UNKNOWN_BLOCKER_BIT_${bit}`, description: `Reserved blocker flag bit ${bit} is set.` })
      }
    }
  }
  return result
}

/**
 * Official Node Operating States from protocol/contracts/sys.yaml, rt.yaml, mtr.yaml
 * (0x500 SYS_NODE_STATUS, 0x501 RT_NODE_STATUS, 0x502 MTR_NODE_STATUS)
 */
export const NODE_STATE_NAMES: Record<number, string> = {
  0: 'INIT',
  1: 'ACQUIRE',
  2: 'STANDBY',
  3: 'ACTIVE',
  4: 'INHIBITED',
  5: 'ESTOP',
  6: 'RECOVER',
  15: 'UNKNOWN',
}

/**
 * Canonical Diagnostic Event States from protocol/contracts/sys.yaml, rt.yaml, mtr.yaml
 * (0x601 SYS_DIAG_EVENT_RPT, 0x621 RT_DIAG_EVENT_RPT, 0x631 MTR_DIAG_EVENT_RPT)
 */
export const DIAG_STATE_NAMES: Record<number, string> = {
  0: 'PENDING',
  1: 'ACTIVE',
  2: 'LATCHED',
  3: 'RECOVERED',
  4: 'CLEARED',
}

/** Decodes 16-bit snapshot_data based on diagnostic code definitions */
export function decodeDiagSnapshot(diagId: number, snapshot: number): string {
  if (snapshot === 0) return 'None'
  
  // CAN Bus Off TEC/REC bitfields: (TEC << 8) | REC
  if (diagId === 0x0109 || diagId === 0x0204 || diagId === 0x0205 || diagId === 0x030D) {
    const tec = (snapshot >> 8) & 0xFF
    const rec = snapshot & 0xFF
    return `TEC: ${tec}, REC: ${rec}`
  }

  // Temperatures in 0.1°C
  if (diagId === 0x010C) {
    return `Temp: ${(snapshot * 0.1).toFixed(1)}°C`
  }

  // Steer following error in 0.1°
  if (diagId === 0x0203) {
    return `Following Error: ${(snapshot * 0.1).toFixed(1)}°`
  }

  // Speed mismatch in mm/s
  if (diagId === 0x0105) {
    return `Delta: ${snapshot} mm/s (${(snapshot * 0.0036).toFixed(1)} km/h)`
  }

  // Elapsed silence in ms for timeouts
  if (
    diagId === 0x0102 ||
    diagId === 0x0103 ||
    diagId === 0x0106 ||
    diagId === 0x0108 ||
    diagId === 0x010E ||
    diagId === 0x0201 ||
    diagId === 0x0202 ||
    diagId === 0x0208 ||
    diagId === 0x0209 ||
    diagId === 0x0212 ||
    diagId === 0x0213 ||
    diagId === 0x0301 ||
    diagId === 0x0305 ||
    diagId === 0x0308 ||
    diagId === 0x0309
  ) {
    return `Elapsed Silence: ${snapshot} ms`
  }

  return `Snapshot Data: 0x${snapshot.toString(16).toUpperCase().padStart(4, '0')} (${snapshot})`
}

/** Checks if diagnostic event report flags mark this event as the initial root cause of emergency stop latching */
export function isRootEstopCause(flags: number): boolean {
  return (flags & 0x01) !== 0
}

/** Parsed protocol diagnostic report event */
export type ParsedDiagEvent = {
  diagId: number
  diagIdHex: string
  def: ProtocolDiagnosticDef | null
  stateNum: number
  stateName: string
  occurrenceCount: number
  reportCounter: number
  flags: number
  isRootEstop: boolean
  snapshotData: number
  decodedSnapshot: string
}

/** Extracts and decodes canonical diagnostic event report from 0x601 / 0x621 / 0x631 */
export function parseDiagEventReport(msg: MessageState): ParsedDiagEvent | null {
  const diagId = signalNum(msg, 'diag_id')
  if (diagId == null) return null

  const stateNum = signalNum(msg, 'state') ?? 1
  const occurrenceCount = signalNum(msg, 'occurrence_count') ?? 1
  const reportCounter = signalNum(msg, 'report_counter') ?? 0
  const flags = signalNum(msg, 'flags') ?? 0
  const snapshotData = signalNum(msg, 'snapshot_data') ?? 0

  const def = PROTOCOL_DIAGNOSTICS_CATALOG[diagId] ?? null
  const idHex = `0x${diagId.toString(16).toUpperCase().padStart(4, '0')}`

  return {
    diagId,
    diagIdHex: idHex,
    def,
    stateNum,
    stateName: DIAG_STATE_NAMES[stateNum] ?? 'UNKNOWN',
    occurrenceCount,
    reportCounter,
    flags,
    isRootEstop: isRootEstopCause(flags),
    snapshotData,
    decodedSnapshot: decodeDiagSnapshot(diagId, snapshotData),
  }
}

/** Parsed 0x115 SYS_ESTOP_RESET_RSP */
export type ParsedEstopResetRsp = {
  requestSeq: number
  resultNum: number
  isRejected: boolean
  blockerMask: number
  blockers: Array<{ bit: number; name: string; description: string }>
}

/** Parses real CAN frame 0x115 SYS_ESTOP_RESET_RSP */
export function parseEstopResetRsp(msg: MessageState): ParsedEstopResetRsp | null {
  const is115 = msg.can_id === 0x115 || msg.name?.includes('ESTOP_RESET')
  if (!is115) return null

  const resText = signalText(msg, 'result')?.toUpperCase()
  const resNum = signalNum(msg, 'result') ?? 0
  const isRejected = resNum === 1 || resText === 'REJECTED'

  const mask = signalNum(msg, 'blocker_mask') ?? 0
  const seq = signalNum(msg, 'request_seq') ?? 0

  return {
    requestSeq: seq,
    resultNum: resNum,
    isRejected,
    blockerMask: mask,
    blockers: decodeBlockerMask(mask),
  }
}

/** Parsed Node Status 0x500 / 0x501 / 0x502 */
export type ParsedNodeStatus = {
  reporter: 'SYS' | 'RT' | 'MTR'
  nodeStateNum: number
  nodeStateName: string
  blockMask: number
  blockers: Array<{ bit: number; name: string; description: string }>
  estopActive: boolean
  estopLatched: boolean
  outputEnabled: boolean
  degraded: boolean
}

/** Parses real CAN frame 0x500 / 0x501 / 0x502 NODE_STATUS */
export function parseNodeStatus(msg: MessageState): ParsedNodeStatus | null {
  let reporter: 'SYS' | 'RT' | 'MTR' | null = null
  if (msg.can_id === 0x500 || msg.name === 'SYS_NODE_STATUS') reporter = 'SYS'
  else if (msg.can_id === 0x501 || msg.name === 'RT_NODE_STATUS') reporter = 'RT'
  else if (msg.can_id === 0x502 || msg.name === 'MTR_NODE_STATUS') reporter = 'MTR'

  if (!reporter) return null

  const stateNum = signalNum(msg, 'node_state') ?? 15
  const blockMask = signalNum(msg, 'block_mask') ?? 0
  const estopActive = signalNum(msg, 'estop_active') === 1
  const estopLatched = signalNum(msg, 'estop_latched') === 1
  const outputEnabled = signalNum(msg, 'output_enabled') === 1
  const degraded = signalNum(msg, 'degraded') === 1

  return {
    reporter,
    nodeStateNum: stateNum,
    nodeStateName: NODE_STATE_NAMES[stateNum] ?? 'UNKNOWN',
    blockMask,
    blockers: decodeBlockerMask(blockMask),
    estopActive,
    estopLatched,
    outputEnabled,
    degraded,
  }
}
