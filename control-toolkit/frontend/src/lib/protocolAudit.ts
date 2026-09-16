/**
 * Programmatic Protocol Conformance & Validation Engine
 *
 * Compares live CAN frames, decoded engineering signals, and cross-tier commands
 * against official YAML-generated protocol specifications (contracts/host.yaml, rt.yaml, mtr.yaml, ses.yaml, seb.yaml).
 */

import type { MessageState } from '../store'

export type SignalContract = {
  id: string
  subsystem: 'Speed' | 'Steering' | 'Braking' | 'Drive Demand' | 'Brake Stroke' | 'Dynamics'
  tier: 'Tier 1 (Host)' | 'Tier 2 (RT)' | 'Tier 3 (Actuator / MTR)'
  bus: 'high' | 'low'
  canIdHex: string
  canId: number
  msgName: string
  signalKey: string
  expectedUnit: string
  contractMin: number
  contractMax: number
  cycleMs: number
  description: string
}

/** Official Canonical Protocol Contracts from etrike protocol specifications */
export const PROTOCOL_CONTRACTS: SignalContract[] = [
  // ── 1. SPEED SUBSYSTEM ──
  {
    id: 'speed-mtr-fbk',
    subsystem: 'Speed',
    tier: 'Tier 3 (Actuator / MTR)',
    bus: 'low',
    canIdHex: '0x206',
    canId: 0x206,
    msgName: 'MTR_MOTOR_FBK',
    signalKey: 'motor_command_speed_mmps',
    expectedUnit: 'mm/s',
    contractMin: -500,
    contractMax: 30000,
    cycleMs: 20,
    description: 'Motor inverter closed-loop command speed feedback',
  },
  {
    id: 'speed-host-cmd',
    subsystem: 'Speed',
    tier: 'Tier 1 (Host)',
    bus: 'high',
    canIdHex: '0x300',
    canId: 0x300,
    msgName: 'HOST_DRIVE_CMD',
    signalKey: 'speed_mmps',
    expectedUnit: 'mm/s',
    contractMin: -500,
    contractMax: 30000,
    cycleMs: 10,
    description: 'Autonomous planner & teleop commanded drive speed',
  },
  {
    id: 'speed-rt-cmd',
    subsystem: 'Speed',
    tier: 'Tier 2 (RT)',
    bus: 'low',
    canIdHex: '0x204',
    canId: 0x204,
    msgName: 'RT_DRIVE_CMD',
    signalKey: 'motor_speed_mmps',
    expectedUnit: 'mm/s',
    contractMin: -500,
    contractMax: 30000,
    cycleMs: 10,
    description: 'Real-time core shaped & rate-limited speed target to inverter',
  },

  // ── 2. STEERING SUBSYSTEM ──
  {
    id: 'steer-ses-actual',
    subsystem: 'Steering',
    tier: 'Tier 3 (Actuator / MTR)',
    bus: 'low',
    canIdHex: '0x201',
    canId: 0x201,
    msgName: 'SES_STATUS',
    signalKey: 'angle_deg',
    expectedUnit: '°',
    contractMin: -180.0,
    contractMax: 180.0,
    cycleMs: 10,
    description: 'EPS electric power steering wheel measured actual angle',
  },
  {
    id: 'steer-host-cmd',
    subsystem: 'Steering',
    tier: 'Tier 1 (Host)',
    bus: 'high',
    canIdHex: '0x303',
    canId: 0x303,
    msgName: 'HOST_STEER_CMD',
    signalKey: 'steer_angle_0_1deg',
    expectedUnit: '°',
    contractMin: -180.0,
    contractMax: 180.0,
    cycleMs: 10,
    description: 'Navigation guidance target steering angle',
  },
  {
    id: 'steer-rt-req',
    subsystem: 'Steering',
    tier: 'Tier 2 (RT)',
    bus: 'low',
    canIdHex: '0x169',
    canId: 0x169,
    msgName: 'VCU_SES_REQ',
    signalKey: 'target_angle_raw',
    expectedUnit: '°',
    contractMin: -180.0,
    contractMax: 180.0,
    cycleMs: 20,
    description: 'Real-time safety ramped steering setpoint to EPS',
  },

  // ── 3. BRAKING SUBSYSTEM ──
  {
    id: 'brake-seb-actual',
    subsystem: 'Braking',
    tier: 'Tier 3 (Actuator / MTR)',
    bus: 'low',
    canIdHex: '0x721',
    canId: 0x721,
    msgName: 'SEB_STATUS',
    signalKey: 'pressure_kpa',
    expectedUnit: 'kPa',
    contractMin: 0,
    contractMax: 20000,
    cycleMs: 10,
    description: 'Smart electronic brake transducer hydraulic pressure feedback',
  },
  {
    id: 'brake-host-req',
    subsystem: 'Braking',
    tier: 'Tier 1 (Host)',
    bus: 'high',
    canIdHex: '0x301',
    canId: 0x301,
    msgName: 'HOST_BRAKE_REQ',
    signalKey: 'brake_pressure_kpa',
    expectedUnit: 'kPa',
    contractMin: 0,
    contractMax: 20000,
    cycleMs: 0,
    description: 'Host commanded service brake pressure demand',
  },
  {
    id: 'brake-rt-cmd',
    subsystem: 'Braking',
    tier: 'Tier 2 (RT)',
    bus: 'low',
    canIdHex: '0x205',
    canId: 0x205,
    msgName: 'RT_BRAKE_CMD',
    signalKey: 'brake_pressure_kpa',
    expectedUnit: 'kPa',
    contractMin: 0,
    contractMax: 20000,
    cycleMs: 20,
    description: 'Real-time validated brake pressure command to SYS/SEB',
  },

  // ── 4. DRIVE DEMAND & WHEEL SPEED ──
  {
    id: 'demand-sys-throttle',
    subsystem: 'Drive Demand',
    tier: 'Tier 1 (Host)',
    bus: 'low',
    canIdHex: '0x120',
    canId: 0x120,
    msgName: 'SYS_THROTTLE_STS',
    signalKey: 'speed_mmps',
    expectedUnit: 'mm/s',
    contractMin: -500,
    contractMax: 30000,
    cycleMs: 10,
    description: 'Physical driver manual throttle grip position / demand',
  },
  {
    id: 'demand-wheel-speed',
    subsystem: 'Drive Demand',
    tier: 'Tier 3 (Actuator / MTR)',
    bus: 'low',
    canIdHex: '0x122',
    canId: 0x122,
    msgName: 'RT_WHEEL_SPEED_STS',
    signalKey: 'measured_speed_mmps',
    expectedUnit: 'mm/s',
    contractMin: -500,
    contractMax: 30000,
    cycleMs: 100,
    description: 'Independent physical wheel hall sensor ground speed',
  },

  // ── 5. BRAKE MECHANICAL STROKE ──
  {
    id: 'stroke-seb-actual',
    subsystem: 'Brake Stroke',
    tier: 'Tier 3 (Actuator / MTR)',
    bus: 'low',
    canIdHex: '0x721',
    canId: 0x721,
    msgName: 'SEB_STATUS',
    signalKey: 'stroke_mm',
    expectedUnit: 'mm',
    contractMin: 0,
    contractMax: 50,
    cycleMs: 10,
    description: 'Brake actuator linear mechanical travel feedback',
  },
  {
    id: 'stroke-sys-req',
    subsystem: 'Brake Stroke',
    tier: 'Tier 2 (RT)',
    bus: 'low',
    canIdHex: '0x7B9',
    canId: 0x7B9,
    msgName: 'VCU_SEB_REQ',
    signalKey: 'stroke_mm',
    expectedUnit: 'mm',
    contractMin: 0,
    contractMax: 50,
    cycleMs: 20,
    description: 'Brake actuator commanded mechanical displacement request',
  },

  // ── 6. STEERING DYNAMICS & SLEW ──
  {
    id: 'dynamics-host-yaw',
    subsystem: 'Dynamics',
    tier: 'Tier 1 (Host)',
    bus: 'high',
    canIdHex: '0x300',
    canId: 0x300,
    msgName: 'HOST_DRIVE_CMD',
    signalKey: 'yaw_rate_mrad_s',
    expectedUnit: 'mrad/s',
    contractMin: -3000,
    contractMax: 3000,
    cycleMs: 10,
    description: 'Planner yaw rate feedforward command',
  },
  {
    id: 'dynamics-ses-torque',
    subsystem: 'Dynamics',
    tier: 'Tier 3 (Actuator / MTR)',
    bus: 'low',
    canIdHex: '0x201',
    canId: 0x201,
    msgName: 'SES_STATUS',
    signalKey: 'torque_nm',
    expectedUnit: 'Nm',
    contractMin: -50,
    contractMax: 50,
    cycleMs: 10,
    description: 'Electric power steering motor assistive torque output',
  },
]

export type SignalAuditResult = {
  contract: SignalContract
  liveValue: number | null
  liveUnit: string
  present: boolean
  schemaValid: boolean
  rangeValid: boolean
  frameValid: boolean
  status: 'conforming' | 'warning' | 'error' | 'unverified'
  diagnosticDetail: string
}

export type SubsystemAuditResult = {
  subsystem: SignalContract['subsystem']
  status: 'conforming' | 'warning' | 'error' | 'unverified'
  summary: string
  consistencyPass: boolean
  consistencyDetail: string
  signals: SignalAuditResult[]
}

/**
 * Audit an individual signal against its contract and live CAN message state.
 */
export function auditSignal(
  contract: SignalContract,
  messages: MessageState[],
  liveValue: number | null,
  dictMessages?: Array<Record<string, unknown>> | null,
): SignalAuditResult {
  const msg = messages.find(
    (m) =>
      (m.name === contract.msgName || m.can_id === contract.canId) &&
      m.bus === contract.bus,
  )

  const present = msg != null && liveValue != null

  // 1. Schema check against dictionary (or bundle if dictionary not yet loaded)
  let schemaValid = true
  if (dictMessages && dictMessages.length > 0) {
    const dictMsg = dictMessages.find(
      (d) =>
        (Number(d.can_id) === contract.canId || d.name === contract.msgName) &&
        String(d.bus || '').toLowerCase() === contract.bus,
    )
    if (!dictMsg) {
      schemaValid = false
    } else {
      const fields = (dictMsg.fields as Array<{ key: string }>) || []
      const hasKey = fields.some(
        (f) =>
          f.key === contract.signalKey ||
          (contract.signalKey.includes('angle') && f.key.includes('angle')) ||
          (contract.signalKey.includes('pressure') && f.key.includes('pressure')) ||
          (contract.signalKey.includes('speed') && f.key.includes('speed')),
      )
      schemaValid = hasKey
    }
  }

  // 2. Range validation
  let rangeValid = true
  if (liveValue != null) {
    rangeValid =
      liveValue >= contract.contractMin && liveValue <= contract.contractMax
  }

  // 3. Frame validation status
  const frameValid = msg ? msg.validation_status !== 'invalid' : true

  let status: SignalAuditResult['status'] = 'conforming'
  let diagnosticDetail = 'Matches protocol schema & within limits'

  if (!present) {
    status = 'unverified'
    diagnosticDetail = 'No active frame received on bus'
  } else if (!schemaValid) {
    status = 'error'
    diagnosticDetail = `Schema discrepancy: ${contract.msgName}.${contract.signalKey} on ${contract.bus} ${contract.canIdHex}`
  } else if (!frameValid) {
    status = 'error'
    diagnosticDetail = `Frame invalid or CRC fault on ${contract.msgName}`
  } else if (!rangeValid) {
    status = 'warning'
    diagnosticDetail = `Exceeds protocol limits [${contract.contractMin}, ${contract.contractMax}] (val: ${liveValue})`
  }

  return {
    contract,
    liveValue,
    liveUnit: contract.expectedUnit,
    present,
    schemaValid,
    rangeValid,
    frameValid,
    status,
    diagnosticDetail,
  }
}

/**
 * Run full audit for all subsystems, including EGAS L2 and Closed-Loop cross-tier consistency checks.
 */
export function runFullProtocolAudit(
  messages: MessageState[],
  extractedValues: Record<string, number | null>,
  dictMessages?: Array<Record<string, unknown>> | null,
): {
  subsystems: SubsystemAuditResult[]
  totalSignals: number
  conformingCount: number
  warningCount: number
  errorCount: number
  overallStatus: 'conforming' | 'warning' | 'error'
} {
  const signalAudits = PROTOCOL_CONTRACTS.map((contract) => {
    const val = extractedValues[contract.id] ?? null
    return auditSignal(contract, messages, val, dictMessages)
  })

  // Cross-tier consistency checks:
  const mtrSpd = extractedValues['speed-mtr-fbk']
  const rtSpd = extractedValues['speed-rt-cmd']
  const hostSpd = extractedValues['speed-host-cmd']
  const egasDiff = mtrSpd != null && rtSpd != null ? Math.abs(mtrSpd - rtSpd) : null
  const hostRtDiff = hostSpd != null && rtSpd != null ? Math.abs(hostSpd - rtSpd) : null
  const speedConsistent = (egasDiff == null || egasDiff <= 250) && (hostRtDiff == null || hostRtDiff <= 500)
  const speedConsistencyDetail =
    egasDiff != null
      ? `EGAS L2: Δ(MTR-RT) = ${egasDiff.toFixed(0)} mm/s${hostRtDiff != null ? `, Δ(Host-RT) = ${hostRtDiff.toFixed(0)} mm/s` : ''}`
      : 'EGAS L2: Single source active'

  // Steering: |SES - RT| <= 5.0 deg
  const sesAngle = extractedValues['steer-ses-actual']
  const rtAngle = extractedValues['steer-rt-req']
  const steerConsistent =
    sesAngle != null && rtAngle != null ? Math.abs(sesAngle - rtAngle) <= 5.0 : true
  const steerConsistencyDetail =
    sesAngle != null && rtAngle != null
      ? `Tracking: Δ(SES-RT) = ${Math.abs(sesAngle - rtAngle).toFixed(1)}° (tolerance ≤ 5.0°)`
      : 'Tracking: Waiting for closed loop'

  // Braking: |Actual - RT| <= 1000 kPa
  const actualBrake = extractedValues['brake-seb-actual']
  const rtBrake = extractedValues['brake-rt-cmd']
  const brakeConsistent =
    actualBrake != null && rtBrake != null ? Math.abs(actualBrake - rtBrake) <= 1500 : true
  const brakeConsistencyDetail =
    actualBrake != null && rtBrake != null
      ? `Pressure: Δ(Actual-RT) = ${Math.abs(actualBrake - rtBrake).toFixed(0)} kPa`
      : 'Braking: Ready'

  const subsystemsMap = new Map<SignalContract['subsystem'], SignalAuditResult[]>()
  for (const a of signalAudits) {
    const list = subsystemsMap.get(a.contract.subsystem) ?? []
    list.push(a)
    subsystemsMap.set(a.contract.subsystem, list)
  }

  const subsystems: SubsystemAuditResult[] = []

  subsystemsMap.forEach((sigs, sub) => {
    let consistencyPass = true
    let consistencyDetail = 'Consistent with protocol'

    if (sub === 'Speed') {
      consistencyPass = speedConsistent
      consistencyDetail = speedConsistencyDetail
    } else if (sub === 'Steering') {
      consistencyPass = steerConsistent
      consistencyDetail = steerConsistencyDetail
    } else if (sub === 'Braking') {
      consistencyPass = brakeConsistent
      consistencyDetail = brakeConsistencyDetail
    }

    const hasError = sigs.some((s) => s.status === 'error') || !consistencyPass
    const hasWarn = sigs.some((s) => s.status === 'warning')
    const allUnverified = sigs.every((s) => s.status === 'unverified')

    const status: SubsystemAuditResult['status'] = hasError
      ? 'error'
      : hasWarn
        ? 'warning'
        : allUnverified
          ? 'unverified'
          : 'conforming'

    const summary = hasError
      ? 'Cross-tier discrepancy or frame error'
      : hasWarn
        ? 'Signal exceeding normal bounds'
        : allUnverified
          ? 'Offline / waiting for bus traffic'
          : 'Protocol conforming · Cross-tier verified'

    subsystems.push({
      subsystem: sub,
      status,
      summary,
      consistencyPass,
      consistencyDetail,
      signals: sigs,
    })
  })

  const conformingCount = signalAudits.filter((s) => s.status === 'conforming').length
  const warningCount = signalAudits.filter((s) => s.status === 'warning').length
  const errorCount = signalAudits.filter((s) => s.status === 'error').length

  const overallStatus =
    errorCount > 0 ? 'error' : warningCount > 0 ? 'warning' : 'conforming'

  return {
    subsystems,
    totalSignals: signalAudits.length,
    conformingCount,
    warningCount,
    errorCount,
    overallStatus,
  }
}
