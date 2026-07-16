import type { MessageState, SignalValue } from './types'

// Canonical message keys used by the Overview workspace, matching
// protocol/contracts/*.yaml via vtc's generated catalog (confirmed live
// against GET /api/v1/protocol/messages — not guessed names).
export const KEYS = {
  safetyEstop: 'safety:safety_estop',
  sysSafetySts: 'sys:sys_safety_sts',
  rtStateRpt: 'rt:rt_state_rpt',
  hmiModeReq: 'hmi:hmi_mode_req',
  hostDriveCmd: 'host:host_drive_cmd',
  rtDriveCmd: 'rt:rt_drive_cmd',
  mtrMotorFbk: 'mtr:mtr_motor_fbk',
  rtBrakeCmd: 'rt:rt_brake_cmd',
  sesStatus: 'ses:ses_status', // opaque custom XOR8 codec — no named signals
  vcuSesReq: 'ses:vcu_ses_req', // opaque custom XOR8 codec — no named signals
} as const

// A canonical key can have multiple physical instances (e.g. same_frame on
// both buses); return the first one seen. Good enough for a summary card —
// the Live CAN workspace shows every physical instance individually.
export function findByKey(messages: MessageState[], key: string): MessageState | undefined {
  return messages.find((m) => m.key === key)
}

export function signal(msg: MessageState | undefined, sigKey: string): SignalValue | undefined {
  return msg?.signals[sigKey]
}

export function readout(sv: SignalValue | undefined): string {
  if (!sv) return '—'
  if (!sv.valid) return 'Invalid'
  if (sv.enum_label) return sv.enum_label
  if (sv.engineering_value !== null) {
    return sv.unit ? `${sv.engineering_value} ${sv.unit}` : String(sv.engineering_value)
  }
  return '—'
}

export function canHealthSummary(messages: MessageState[]): { live: number; late: number; missing: number; invalid: number; total: number } {
  const out = { live: 0, late: 0, missing: 0, invalid: 0, total: messages.length }
  for (const m of messages) {
    if (m.freshness === 'live') out.live++
    else if (m.freshness === 'late') out.late++
    else if (m.freshness === 'missing') out.missing++
    else if (m.freshness === 'invalid') out.invalid++
  }
  return out
}
