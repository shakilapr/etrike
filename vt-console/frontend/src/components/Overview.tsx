import { useAppStore } from '../store'
import { canHealthSummary, findByKey, KEYS, readout, signal } from '../catalog'
import { Card, StatusPill, Unknown } from './primitives'
import { FRESHNESS_CLASS, FRESHNESS_LABEL } from '../freshness'

// Read-only vehicle-state overview (workplan §4.4), built entirely from real
// decoded signals already in GET /api/v1/state — no fields are invented.
// Fields the spec names but that have no backend source yet (confirmed
// mode/power, control-path) render as Unknown with a comment on which future
// phase owns them, per the "never fabricate" rule.
export function Overview() {
  const messages = useAppStore((s) => s.messages)

  const sysSafety = findByKey(messages, KEYS.sysSafetySts)
  const rtState = findByKey(messages, KEYS.rtStateRpt)
  const estopEvent = findByKey(messages, KEYS.safetyEstop)
  const hmiMode = findByKey(messages, KEYS.hmiModeReq)

  const estopActive = signal(sysSafety, 'estop_active')
  const requestedMode = signal(hmiMode, 'req_mode')
  const confirmedMode = signal(rtState, 'mode')

  const hostDrive = findByKey(messages, KEYS.hostDriveCmd)
  const mtrFbk = findByKey(messages, KEYS.mtrMotorFbk)
  const rtBrake = findByKey(messages, KEYS.rtBrakeCmd)
  const sesStatus = findByKey(messages, KEYS.sesStatus)

  const health = canHealthSummary(messages)

  return (
    <div className="flex flex-col gap-4 p-4">
      <div className="grid grid-cols-4 gap-3">
        <Card title="ESTOP">
          {estopActive ? (
            <StatusPill
              label={estopActive.engineering_value ? 'Active' : 'Clear'}
              colorClass={estopActive.engineering_value ? 'text-danger bg-danger-soft' : 'text-live bg-live/10'}
            />
          ) : (
            <Unknown />
          )}
          {estopEvent && (
            <div className="mt-1 text-xs text-text-faint">
              last event: {FRESHNESS_LABEL[estopEvent.freshness]}
            </div>
          )}
        </Card>

        <Card title="Mode (requested / confirmed)">
          <div className="text-sm">
            {readout(requestedMode)} <span className="text-text-faint">/</span> {readout(confirmedMode)}
          </div>
        </Card>

        <Card title="Power (requested / confirmed)">
          {/* HMI_PWR_REQ exists on the bus but Phase 4 doesn't wire a
              "confirmed power" source yet — no RT/SYS signal reports it
              directly. Owned by Phase 5/6 (HMI control + diagnostics). */}
          <Unknown />
        </Card>

        <Card title="Control path">
          {/* "Who is driving" (Host kinematics vs direct actuator vs bench)
              is Phase 7 scope — needs the control-intent service. */}
          <Unknown />
        </Card>
      </div>

      <div className="grid grid-cols-4 gap-3">
        <Card title="Speed (feedback / commanded)">
          <div className="font-mono text-lg">
            {readout(signal(mtrFbk, 'actual_speed_mmps'))}
            <span className="mx-1 text-sm text-text-faint">/</span>
            <span className="text-sm">{readout(signal(hostDrive, 'speed_mmps'))}</span>
          </div>
        </Card>
        <Card title="Gear (feedback / commanded)">
          <div className="text-sm">
            {readout(signal(mtrFbk, 'gear_state'))} <span className="text-text-faint">/</span> {readout(signal(hostDrive, 'gear'))}
          </div>
        </Card>
        <Card title="Brake (commanded)">
          <div className="text-sm">{readout(signal(rtBrake, 'brake_pressure_kpa'))}</div>
          <div className="mt-1 text-xs text-text-faint">feedback: SEB is a custom vendor codec, not decoded as named signals</div>
        </Card>
        <Card title="Steering">
          {sesStatus ? (
            <StatusPill label={FRESHNESS_LABEL[sesStatus.freshness]} colorClass={FRESHNESS_CLASS[sesStatus.freshness]} />
          ) : (
            <Unknown />
          )}
          <div className="mt-1 text-xs text-text-faint">SES is a custom vendor codec (XOR8); freshness shown, values are not</div>
        </Card>
      </div>

      <Card title="CAN health">
        <div className="flex gap-4 text-sm">
          <span className="text-live">{health.live} live</span>
          <span className="text-late">{health.late} late</span>
          <span className="text-missing">{health.missing} missing</span>
          <span className="text-invalid">{health.invalid} invalid</span>
          <span className="text-text-faint">{health.total} total observed</span>
        </div>
      </Card>

      {rtState && signal(rtState, 'safety_state') && (
        <Card title="RT safety state">
          <div className="text-sm">{readout(signal(rtState, 'safety_state'))}</div>
        </Card>
      )}
    </div>
  )
}
