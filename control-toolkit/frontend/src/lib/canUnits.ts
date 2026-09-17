/**
 * Canonical CAN Unit mapping and filter definitions.
 *
 * Sourced directly from protocol/contracts/ and generated DBC specs:
 * - Host: Host commands & HMI requests (High bus / bridged Low)
 * - RT-H: Real-time controller on High bus (motion reports, state, diagnostics)
 * - RT-L: Real-time controller on Low bus (actuator commands, state reports)
 * - SYS: Safety & system supervisor (safety status, power/mode commands, SEB requests)
 * - MTR: Motor inverter / controller (feedback, throttle status, diagnostics)
 * - SBW: Steering-by-wire (SES / SES status, error info)
 * - BBW: Brake-by-wire (SEB status, error info)
 */

export type CanUnit = 'Host' | 'RT-H' | 'RT-L' | 'SYS' | 'MTR' | 'SBW' | 'BBW'
export type CanUnitFilter = 'all' | CanUnit

export const CAN_UNIT_OPTIONS: readonly { value: CanUnitFilter; label: string }[] = [
  { value: 'all', label: 'All units' },
  { value: 'Host', label: 'Host' },
  { value: 'RT-H', label: 'RT-H' },
  { value: 'RT-L', label: 'RT-L' },
  { value: 'SYS', label: 'SYS' },
  { value: 'MTR', label: 'MTR' },
  { value: 'SBW', label: 'SBW' },
  { value: 'BBW', label: 'BBW' },
] as const

/**
 * Resolve which vehicle unit or subsystem originates / owns a CAN message.
 */
export function getMessageUnit(
  bus: string,
  canId: number,
  name?: string | null,
): CanUnit | null {
  const normBus = (bus || '').toLowerCase()
  const normName = (name || '').toUpperCase()

  // 1. SBW (Steering By Wire: SES / SES)
  if (
    canId === 0x201 ||
    canId === 0x202 ||
    canId === 0x203 ||
    canId === 0x6fa ||
    normName.startsWith('SES_') ||
    normName.startsWith('SBW_') ||
    normName === 'VCU_SES_REQ' && normBus === 'none'
  ) {
    return 'SBW'
  }

  // 2. BBW (Brake By Wire: SEB)
  if (
    canId === 0x6fb ||
    canId === 0x721 ||
    canId === 0x731 ||
    canId === 0x741 ||
    normName.startsWith('SEB_') ||
    normName.startsWith('BBW_')
  ) {
    return 'BBW'
  }

  // 3. SYS (System / Safety Supervisor)
  if (
    canId === 0x011 ||
    canId === 0x110 ||
    canId === 0x113 ||
    canId === 0x115 ||
    canId === 0x500 ||
    canId === 0x600 ||
    canId === 0x601 ||
    canId === 0x7b9 ||
    canId === 0x7fe ||
    normName.startsWith('SYS_') ||
    normName === 'VCU_SEB_REQ'
  ) {
    return 'SYS'
  }

  // 4. MTR (Motor Controller / Inverter)
  if (
    canId === 0x120 ||
    canId === 0x206 ||
    canId === 0x502 ||
    canId === 0x631 ||
    normName.startsWith('MTR_')
  ) {
    return 'MTR'
  }

  // 5. Host (Host Computer & HMI Requests)
  if (
    canId === 0x111 ||
    canId === 0x112 ||
    canId === 0x114 ||
    canId === 0x300 ||
    canId === 0x301 ||
    canId === 0x302 ||
    canId === 0x303 ||
    canId === 0x400 ||
    canId === 0x7fc ||
    normName.startsWith('HOST_') ||
    normName.startsWith('HMI_')
  ) {
    return 'Host'
  }

  // 6. RT (Real-Time Controller on High or Low bus)
  if (
    canId === 0x121 ||
    canId === 0x122 ||
    canId === 0x169 ||
    canId === 0x204 ||
    canId === 0x205 ||
    canId === 0x210 ||
    canId === 0x220 ||
    canId === 0x310 ||
    canId === 0x311 ||
    canId === 0x501 ||
    canId === 0x620 ||
    canId === 0x621 ||
    canId === 0x7fd ||
    normName.startsWith('RT_') ||
    normName === 'VCU_SES_REQ' ||
    normName === 'STEER_DIAG' ||
    normName === 'BRAKE_DIAG'
  ) {
    return normBus === 'high' ? 'RT-H' : 'RT-L'
  }

  // Fallbacks by naming prefix
  if (normName.startsWith('RT_')) return normBus === 'high' ? 'RT-H' : 'RT-L'
  if (normName.startsWith('SYS_')) return 'SYS'
  if (normName.startsWith('MTR_')) return 'MTR'
  if (normName.startsWith('HOST_') || normName.startsWith('HMI_')) return 'Host'
  if (normName.startsWith('SES_') || normName.startsWith('SBW_')) return 'SBW'
  if (normName.startsWith('SEB_') || normName.startsWith('BBW_')) return 'BBW'

  return null
}
