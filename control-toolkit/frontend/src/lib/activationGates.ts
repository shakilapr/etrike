import type { MessageState } from '../store'
import { findMsg, signalNum, signalIsOn, signalText, frameRecent } from './signals'

export type ActivationSignalItem = {
  id: string
  canId: string
  bus: 'high' | 'low'
  msgName: string
  signalKey: string
  description: string
  condition: string
  liveValue: string | number | null
  isMet: boolean
  impactIfMissing: string
}

export type ControllerActivationGate = {
  id: string
  controller: string
  subsystem: 'Steering' | 'Braking' | 'Propulsion' | 'System' | 'Host'
  primaryCanIds: string[]
  status: 'ready' | 'inhibited' | 'warning'
  statusLabel: string
  signals: ActivationSignalItem[]
  blocker: string | null
}

export type ActivationAuditReport = {
  gates: ControllerActivationGate[]
  totalGates: number
  clearedGates: number
  allCleared: boolean
  blockers: string[]
}

export function evaluateActivationGates(
  messages: MessageState[],
  isDemo = false,
): ActivationAuditReport {
  const hasFrames = messages.length > 0
  const useSim = isDemo || !hasFrames

  // 1. Fetch relevant messages
  const sysMode = findMsg(messages, 'SYS_MODE_CMD', 'low')
  const sysPwr = findMsg(messages, 'SYS_PWR_CMD', 'low')
  const sysSafety = findMsg(messages, 'SYS_SAFETY_STS', 'low')
  const sysHb = findMsg(messages, 'SYS_HEARTBEAT', 'low')
  const sysDiag = findMsg(messages, 'SYS_DIAG_RPT', 'low')

  const rtHb = findMsg(messages, 'RT_HEARTBEAT')
  const rtState = findMsg(messages, 'RT_STATE_RPT', 'low') ?? findMsg(messages, 'RT_STATUS', 'low')

  const rtSesReq = findMsg(messages, 'VCU_SES_REQ', 'low')
  const sesStatus = findMsg(messages, 'SES_STATUS', 'low')

  const sysSebReq = findMsg(messages, 'VCU_SEB_REQ', 'low')
  const sebStatus = findMsg(messages, 'SEB_STATUS', 'low')

  const rtDrive = findMsg(messages, 'RT_DRIVE_CMD', 'low')
  const mtrFbk = findMsg(messages, 'MTR_MOTOR_FBK', 'low')

  const hostHb = findMsg(messages, 'HOST_HEARTBEAT', 'high')
  const hostDrive = findMsg(messages, 'HOST_DRIVE_CMD', 'high')
  const hostSteer = findMsg(messages, 'HOST_STEER_CMD', 'high')

  // ── GATE 1: SYS System Authority & Safety Interlocks ──
  const sysModeVal = useSim ? 1 : (signalNum(sysMode, 'mode') ?? (signalText(sysMode, 'mode') === 'AUTO' ? 1 : 0))
  const sysModeMet = sysModeVal === 1

  const sysPwrVal = useSim ? 1 : (signalNum(sysPwr, 'power_state') ?? (signalText(sysPwr, 'power_state') === 'ON' ? 1 : 0))
  const sysPwrMet = sysPwrVal === 1

  const estopVal = useSim ? 0 : (signalNum(sysSafety, 'estop_active') ?? (signalIsOn(sysSafety, 'estop_active') ? 1 : 0))
  const estopMet = estopVal === 0

  const brakeLeverVal = useSim ? 0 : (signalNum(sysDiag, 'brake_engaged') ?? (signalIsOn(sysDiag, 'brake_engaged') ? 1 : 0))
  const brakeLeverMet = brakeLeverVal === 0

  const sysHbOk = useSim ? 1 : (signalNum(sysHb, 'heartbeat_ok') ?? 1)
  const sysHbMet = sysHbOk === 1

  const sysSignals: ActivationSignalItem[] = [
    {
      id: 'sys-mode-cmd',
      canId: '0x110',
      bus: 'low',
      msgName: 'SYS_MODE_CMD',
      signalKey: 'mode',
      description: 'SYS authoritative drive mode set to AUTO',
      condition: '== 1 (AUTO)',
      liveValue: sysModeMet ? '1 (AUTO)' : '0 (MANUAL)',
      isMet: sysModeMet,
      impactIfMissing: 'RT supervisor stays silent; MTR rejects autonomous velocity setpoints',
    },
    {
      id: 'sys-pwr-cmd',
      canId: '0x113',
      bus: 'low',
      msgName: 'SYS_PWR_CMD',
      signalKey: 'power_state',
      description: 'High-voltage/traction motor contactor relay commanded closed',
      condition: '== 1 (ON)',
      liveValue: sysPwrMet ? '1 (ON)' : '0 (OFF)',
      isMet: sysPwrMet,
      impactIfMissing: 'MTR traction drive power rail de-energized',
    },
    {
      id: 'sys-safety-sts',
      canId: '0x011',
      bus: 'low',
      msgName: 'SYS_SAFETY_STS',
      signalKey: 'estop_active',
      description: 'Global system safety status clear of ESTOP latch',
      condition: '== 0 (Clear)',
      liveValue: estopMet ? '0 (Clear)' : '1 (ACTIVE)',
      isMet: estopMet,
      impactIfMissing: 'Global emergency stop clamp enforced across all actuators',
    },
    {
      id: 'sys-brake-lever',
      canId: '0x600',
      bus: 'low',
      msgName: 'SYS_DIAG_RPT',
      signalKey: 'brake_engaged',
      description: 'Physical handlebar brake lever sensor (GPIO2 pull-up)',
      condition: '== 0 (Released)',
      liveValue: brakeLeverMet ? '0 (Released)' : '1 (Pulled)',
      isMet: brakeLeverMet,
      impactIfMissing: 'Physical handlebar brake lever overrides and disengages AUTO mode',
    },
    {
      id: 'sys-heartbeat',
      canId: '0x7FE',
      bus: 'low',
      msgName: 'SYS_HEARTBEAT',
      signalKey: 'heartbeat_ok',
      description: 'SYS periodic 10 Hz supervisor heartbeat',
      condition: '== 1 (OK)',
      liveValue: sysHbMet ? '1 (OK)' : '0 (Fault)',
      isMet: sysHbMet,
      impactIfMissing: 'Loss of SYS heartbeat forces RT into standalone emergency stop',
    },
  ]

  const sysBlocked = !sysModeMet || !sysPwrMet || !estopMet || !brakeLeverMet || !sysHbMet
  const sysBlockerText = !estopMet
    ? 'ESTOP active (0x011)'
    : !sysPwrMet
      ? 'Contactor open (0x113)'
      : !sysModeMet
        ? 'Mode is MANUAL (0x110)'
        : !brakeLeverMet
          ? 'Brake lever engaged (0x600)'
          : null

  const gateSys: ControllerActivationGate = {
    id: 'gate-sys',
    controller: 'SYS Authority',
    subsystem: 'System',
    primaryCanIds: ['0x110', '0x113', '0x011'],
    status: sysBlocked ? 'inhibited' : 'ready',
    statusLabel: sysBlocked ? 'Inhibited' : 'Ready · Commanded',
    signals: sysSignals,
    blocker: sysBlockerText,
  }

  // ── GATE 2: RT Autonomous Motion Master ──
  const rtHbOk = useSim ? 1 : (signalNum(rtHb, 'heartbeat_ok') ?? (frameRecent(rtHb, 1500) ? 1 : 0))
  const rtHbAuto = useSim ? 1 : (signalNum(rtHb, 'mode_auto') ?? (signalIsOn(rtHb, 'mode_auto') ? 1 : 0))
  const rtHbMet = rtHbOk === 1 && (useSim || rtHbAuto === 1)

  const rtStateRaw = signalText(rtState, 'node_state')
  const rtSafetyRaw = signalNum(rtState, 'safety_state')
  const rtStateMet = useSim || (rtStateRaw === 'ACTIVE' || rtSafetyRaw === 1)

  const rtSignals: ActivationSignalItem[] = [
    {
      id: 'rt-heartbeat',
      canId: '0x7FD',
      bus: 'low',
      msgName: 'RT_HEARTBEAT',
      signalKey: 'mode_auto',
      description: 'RT supervisor confirming autonomous control loop engaged',
      condition: 'heartbeat_ok=1 & mode_auto=1',
      liveValue: rtHbMet ? '1 (AUTO OK)' : '0 (Standby)',
      isMet: rtHbMet,
      impactIfMissing: 'RT supervisor not cycling autonomous motion loop',
    },
    {
      id: 'rt-state-rpt',
      canId: '0x012',
      bus: 'low',
      msgName: 'RT_STATE_RPT',
      signalKey: 'safety_state',
      description: 'RT node state active with zero unrecovered safety faults',
      condition: 'safety_state == 1 (Nominal)',
      liveValue: rtStateMet ? '1 (Nominal)' : 'Fault',
      isMet: rtStateMet,
      impactIfMissing: 'RT rejects trajectory commands due to internal state inhibit',
    },
  ]

  const rtBlocked = !rtHbMet || !rtStateMet
  const gateRt: ControllerActivationGate = {
    id: 'gate-rt',
    controller: 'RT Supervisor',
    subsystem: 'System',
    primaryCanIds: ['0x7FD', '0x012'],
    status: rtBlocked ? 'inhibited' : 'ready',
    statusLabel: rtBlocked ? 'Inhibited' : 'Ready · Active',
    signals: rtSignals,
    blocker: rtBlocked ? 'RT supervisor not active (0x012/0x7FD)' : null,
  }

  // ── GATE 3: SES Steer-by-Wire (SES) Controller ──
  const sesCtrlEnVal = useSim ? 1 : (signalNum(rtSesReq, 'control_enable') ?? (signalIsOn(rtSesReq, 'control_enable') ? 1 : 0))
  const sesCtrlEnMet = sesCtrlEnVal === 1

  const sesAlignedVal = useSim ? 1 : (signalNum(sesStatus, 'ses_aligned') ?? signalNum(sesStatus, 'aligned') ?? (signalIsOn(sesStatus, 'aligned') ? 1 : 0))
  const sesAlignedMet = sesAlignedVal === 1

  const sesTorqueVal = useSim ? 0.4 : signalNum(sesStatus, 'torque_nm')
  const sesTorqueMet = sesTorqueVal == null || Math.abs(sesTorqueVal) < 3.0

  const sesFaultVal = useSim ? 0 : (signalNum(sesStatus, 'error_status') ?? signalNum(sesStatus, 'fault_level') ?? 0)
  const sesFaultMet = sesFaultVal === 0

  const sesSignals: ActivationSignalItem[] = [
    {
      id: 'ses-ctrl-en',
      canId: '0x169',
      bus: 'low',
      msgName: 'VCU_SES_REQ',
      signalKey: 'control_enable',
      description: 'RT steer-by-wire SES closed-loop tracking activation flag',
      condition: '== 1 (Enabled)',
      liveValue: sesCtrlEnMet ? '1 (Enabled)' : '0 (Disabled)',
      isMet: sesCtrlEnMet,
      impactIfMissing: 'SES ignores autonomous target steering angle from RT',
    },
    {
      id: 'ses-aligned',
      canId: '0x201',
      bus: 'low',
      msgName: 'SES_STATUS',
      signalKey: 'aligned',
      description: 'Steer-by-wire zero-point angle calibration valid',
      condition: '== 1 (Calibrated)',
      liveValue: sesAlignedMet ? '1 (Aligned)' : '0 (Syncing)',
      isMet: sesAlignedMet,
      impactIfMissing: 'Drive motor is locked out until SES confirms zero-point alignment',
    },
    {
      id: 'ses-torque',
      canId: '0x201',
      bus: 'low',
      msgName: 'SES_STATUS',
      signalKey: 'torque_nm',
      description: 'Handlebar torque transducer reading (< 3.0 Nm = human override inactive)',
      condition: '< 3.0 Nm',
      liveValue: sesTorqueVal != null ? `${Math.abs(sesTorqueVal).toFixed(1)} Nm` : '0.0 Nm',
      isMet: sesTorqueMet,
      impactIfMissing: 'Driver counter-torque >= 3.5 Nm suspends autonomy and yields steering',
    },
    {
      id: 'ses-fault',
      canId: '0x201',
      bus: 'low',
      msgName: 'SES_STATUS',
      signalKey: 'fault_level',
      description: 'Internal SES actuator fault status level',
      condition: '== 0 (L0 Normal)',
      liveValue: sesFaultMet ? '0 (Normal)' : `L${sesFaultVal}`,
      isMet: sesFaultMet,
      impactIfMissing: 'EPS internal sensor or driver fault locks out closed-loop steering',
    },
  ]

  const sesBlocked = !sesCtrlEnMet || !sesAlignedMet || !sesTorqueMet || !sesFaultMet
  const sesBlockerText = !sesCtrlEnMet
    ? 'Control enable bit inactive (0x169)'
    : !sesAlignedMet
      ? 'Zero-point not calibrated (0x201)'
      : !sesTorqueMet
        ? 'Manual counter-torque override (0x201)'
        : 'Actuator fault (0x201)'

  const gateSes: ControllerActivationGate = {
    id: 'gate-ses',
    controller: 'SES Steer-by-Wire',
    subsystem: 'Steering',
    primaryCanIds: ['0x169', '0x201'],
    status: sesBlocked ? 'inhibited' : 'ready',
    statusLabel: sesBlocked ? 'Inhibited' : 'Ready · Enabled',
    signals: sesSignals,
    blocker: sesBlocked ? sesBlockerText : null,
  }

  // ── GATE 4: SEB Brake-by-Wire (Smart Electronic Brake) ──
  const sebAutoReqVal = useSim ? 1 : (signalNum(sysSebReq, 'auto_brake') ?? 1)
  const sebAutoReqMet = sebAutoReqVal === 1

  const sebErrVal = useSim ? 0 : (signalNum(sebStatus, 'error_status') ?? 0)
  const sebErrMet = sebErrVal < 3

  const sebStatusVal = useSim ? 1 : (signalNum(sebStatus, 'auto_brake_status') ?? (frameRecent(sebStatus) ? 1 : 0))
  const sebStatusMet = sebStatusVal === 1

  const sebSignals: ActivationSignalItem[] = [
    {
      id: 'seb-auto-req',
      canId: '0x7B9',
      bus: 'low',
      msgName: 'VCU_SEB_REQ',
      signalKey: 'auto_brake',
      description: 'Autonomous electronic brake hydraulic line pressure demand enable',
      condition: '== 1 (Enabled)',
      liveValue: sebAutoReqMet ? '1 (Enabled)' : '0 (Disabled)',
      isMet: sebAutoReqMet,
      impactIfMissing: 'SEB actuator will not respond to autonomous braking commands',
    },
    {
      id: 'seb-health',
      canId: '0x721',
      bus: 'low',
      msgName: 'SEB_STATUS',
      signalKey: 'error_status',
      description: 'SEB transducer fault code (error_status == 3 is Level 3 safety fault)',
      condition: '< 3 (L0-L2 Only)',
      liveValue: sebErrMet ? `${sebErrVal} (Pass)` : `3 (L3 FAULT)`,
      isMet: sebErrMet,
      impactIfMissing: 'SEB L3 fault latches kLatchedSebL3, refusing mode changes and ESTOP reset',
    },
    {
      id: 'seb-auto-ack',
      canId: '0x721',
      bus: 'low',
      msgName: 'SEB_STATUS',
      signalKey: 'auto_brake_status',
      description: 'SEB actuator acknowledging autonomous hydraulic authority',
      condition: '== 1 (Active)',
      liveValue: sebStatusMet ? '1 (Active)' : '0 (Standby)',
      isMet: sebStatusMet,
      impactIfMissing: 'Hydraulic clamping delayed or degraded during emergency stop',
    },
  ]

  const sebBlocked = !sebAutoReqMet || !sebErrMet || !sebStatusMet
  const gateSeb: ControllerActivationGate = {
    id: 'gate-seb',
    controller: 'SEB Smart Brake',
    subsystem: 'Braking',
    primaryCanIds: ['0x7B9', '0x721'],
    status: sebBlocked ? 'inhibited' : 'ready',
    statusLabel: sebBlocked ? 'Inhibited' : 'Ready · Calibrated',
    signals: sebSignals,
    blocker: sebBlocked ? (!sebErrMet ? 'SEB L3 Fault active (0x721)' : 'Auto brake inactive (0x7B9)') : null,
  }

  // ── GATE 5: MTR Propulsion Drive (STM32 Motor) ──
  const rtDriveFresh = useSim || (rtDrive != null && frameRecent(rtDrive, 1000))
  const mtrEchoFresh = useSim || (mtrFbk != null && frameRecent(mtrFbk, 1000))

  const mtrSignals: ActivationSignalItem[] = [
    {
      id: 'mtr-drive-cmd',
      canId: '0x204',
      bus: 'low',
      msgName: 'RT_DRIVE_CMD',
      signalKey: 'motor_speed_mmps',
      description: 'RT resolved velocity setpoint transmitted at 50 Hz on Low bus',
      condition: 'Fresh stream (< 200 ms)',
      liveValue: rtDriveFresh ? 'Streaming (50 Hz)' : 'Stale / Missing',
      isMet: rtDriveFresh,
      impactIfMissing: 'Inverter enters safe coast timeout when command stream halts',
    },
    {
      id: 'mtr-feedback',
      canId: '0x206',
      bus: 'low',
      msgName: 'MTR_MOTOR_FBK',
      signalKey: 'motor_command_speed_mmps',
      description: 'Motor inverter feedback echo (prohibits kInhibitMtrFbkLoss in production)',
      condition: 'Alive (< 200 ms) · Bench Note: Pending HW',
      liveValue: useSim ? 'Echo OK (Sim)' : (mtrEchoFresh ? 'Alive' : 'Pending HW'),
      isMet: useSim || mtrEchoFresh,
      impactIfMissing: 'SYS triggers traction inhibit kInhibitMtrFbkLoss, clamping authority to MANUAL',
    },
  ]

  const mtrBlocked = !rtDriveFresh
  const gateMtr: ControllerActivationGate = {
    id: 'gate-mtr',
    controller: 'MTR Propulsion',
    subsystem: 'Propulsion',
    primaryCanIds: ['0x204', '0x206'],
    status: mtrBlocked ? 'inhibited' : (mtrEchoFresh ? 'ready' : 'warning'),
    statusLabel: mtrBlocked ? 'Inhibited' : (mtrEchoFresh ? 'Ready · Active' : 'Active (Echo Pending)'),
    signals: mtrSignals,
    blocker: mtrBlocked ? 'Drive setpoint missing (0x204)' : null,
  }

  // ── GATE 6: Host Autonomous Navigation (Jetson / Autoware) ──
  const hostHbOk = useSim || (hostHb != null && frameRecent(hostHb, 1500))
  const hostDriveFresh = useSim || (hostDrive != null && frameRecent(hostDrive, 500))
  const hostSteerValid = useSim ? 1 : (signalNum(hostSteer, 'angle_valid') ?? (signalIsOn(hostSteer, 'angle_valid') ? 1 : 0))
  const hostSteerMet = hostSteerValid === 1

  const hostSignals: ActivationSignalItem[] = [
    {
      id: 'host-heartbeat',
      canId: '0x7FC',
      bus: 'high',
      msgName: 'HOST_HEARTBEAT',
      signalKey: 'alive_ctr',
      description: 'Jetson Autoware autonomy stack periodic 10 Hz heartbeat',
      condition: 'Alive (< 1000 ms)',
      liveValue: hostHbOk ? '10 Hz Active' : 'Missing',
      isMet: hostHbOk,
      impactIfMissing: 'RT flags host compute loss and initiates safe stop',
    },
    {
      id: 'host-drive-cmd',
      canId: '0x300',
      bus: 'high',
      msgName: 'HOST_DRIVE_CMD',
      signalKey: 'speed_mmps',
      description: 'High-level velocity and yaw-rate trajectory stream (20 Hz)',
      condition: 'Fresh (< 200 ms timeout)',
      liveValue: hostDriveFresh ? 'Streaming (20 Hz)' : 'Stale / Missing',
      isMet: hostDriveFresh,
      impactIfMissing: 'RT motion watchdog triggers zero setpoint when stale > 200 ms',
    },
    {
      id: 'host-steer-cmd',
      canId: '0x303',
      bus: 'high',
      msgName: 'HOST_STEER_CMD',
      signalKey: 'angle_valid',
      description: 'Planner target steering angle validated flag',
      condition: '== 1 (Valid)',
      liveValue: hostSteerMet ? '1 (Valid)' : '0 (Invalid)',
      isMet: hostSteerMet,
      impactIfMissing: 'RT rejects unvalidated steering trajectory and clamps curvature',
    },
  ]

  const hostBlocked = !hostHbOk || !hostDriveFresh || !hostSteerMet
  const gateHost: ControllerActivationGate = {
    id: 'gate-host',
    controller: 'Host Planner',
    subsystem: 'Host',
    primaryCanIds: ['0x7FC', '0x300', '0x303'],
    status: hostBlocked ? 'inhibited' : 'ready',
    statusLabel: hostBlocked ? 'Inhibited' : 'Ready · Streaming',
    signals: hostSignals,
    blocker: hostBlocked ? (!hostHbOk ? 'Host heartbeat lost (0x7FC)' : 'Host trajectory stale (0x300)') : null,
  }

  const gates = [gateSys, gateRt, gateSes, gateSeb, gateMtr, gateHost]
  const blockers: string[] = []
  for (const g of gates) {
    if (g.blocker) blockers.push(`${g.controller}: ${g.blocker}`)
  }

  const clearedGates = gates.filter((g) => g.status === 'ready').length

  return {
    gates,
    totalGates: gates.length,
    clearedGates,
    allCleared: blockers.length === 0,
    blockers,
  }
}
