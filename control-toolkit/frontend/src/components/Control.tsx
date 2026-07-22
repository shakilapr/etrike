import { useCallback, useEffect, useRef, useState } from 'react'
import { api } from '../api'
import { useActiveTxStore } from '../lib/activeTxStore'
import { cleanupControlStreams, isStaleSequenceError } from '../lib/cleanup'
import { findMsg, signalText } from '../lib/signals'
import { useAppStore, type ControlMethod, type Status } from '../store'
import { NumericDraft } from './NumericDraft'
import { WorkspaceShell } from './WorkspaceShell'

function DirectActuatorCards({
  busy,
  setBusy,
  setLog,
  ensureSessionReady,
  refresh,
  disabled,
  motorOnly = false,
}: {
  busy: boolean
  setBusy: (b: boolean) => void
  setLog: (s: string) => void
  ensureSessionReady: () => Promise<Status>
  refresh: () => Promise<Status>
  disabled?: boolean
  motorOnly?: boolean
}) {
  const messages = useAppStore((s) => s.messages)
  const [motorSpeed, setMotorSpeed] = useState(300)
  const [motorGear, setMotorGear] = useState(1)
  const [steerAngle, setSteerAngle] = useState(50)
  const [brakePressure, setBrakePressure] = useState(40)
  const [active, setActive] = useState<Record<string, boolean>>({})
  const [lastCtrl, setLastCtrl] = useState<Record<string, unknown> | null>(null)

  // TX commands we send (not only ECU feedback — pure software often has no FBK peers)
  const txMotor = findMsg(messages, 'RT_DRIVE_CMD', 'low')
  const txSteer = findMsg(messages, 'VCU_SES_REQ', 'low')
  const txBrake = findMsg(messages, 'VCU_SEB_REQ', 'low')
  const fbkMtr = findMsg(messages, 'MTR_MOTOR_FBK')
  const fbkSes = findMsg(messages, 'SES_STATUS')
  const fbkSeb = findMsg(messages, 'SEB_STATUS')

  // Sync active chips from backend control status
  useEffect(() => {
    let cancel = false
    const tick = async () => {
      try {
        const c = await api.controlStatus()
        if (cancel) return
        setLastCtrl(c.control)
        const ch = (c.control?.direct_channels as string[] | undefined) || []
        setActive({
          motor: ch.includes('motor'),
          steering: ch.includes('steering'),
          brake: ch.includes('brake'),
        })
      } catch {
        /* ignore */
      }
    }
    void tick()
    const id = window.setInterval(() => void tick(), 1000)
    return () => {
      cancel = true
      window.clearInterval(id)
    }
  }, [])

  // Heartbeat direct channels so backend 500ms watchdog does not kill streams
  // while this tab is open. Leaving the page / crash → streams stop.
  useEffect(() => {
    const channels = (['motor', 'steering', 'brake'] as const).filter((c) => active[c])
    if (!channels.length) return
    const beat = () => {
      for (const channel of channels) {
        const values =
          channel === 'motor'
            ? { motor_speed_mmps: motorSpeed, gear: motorGear }
            : channel === 'steering'
              ? {
                  target_angle_raw: steerAngle,
                  control_enable: true,
                  alignment_enable: true,
                }
              : {
                  pressure_request_raw: brakePressure,
                  control_enable: true,
                  alignment_enable: true,
                  control_mode: 1,
                }
        void api
          .controlDirect({
            channel,
            enabled: true,
            values,
            period_ms: channel === 'motor' ? 10 : 20,
          })
          .catch(() => undefined)
      }
    }
    const id = window.setInterval(beat, 250)
    return () => window.clearInterval(id)
  }, [active, motorSpeed, motorGear, steerAngle, brakePressure])

  async function start(channel: 'motor' | 'steering' | 'brake') {
    setBusy(true)
    try {
      await ensureSessionReady()
      // Safety bypass: control_enable + alignment_enable always forced ON in backend.
      const values =
        channel === 'motor'
          ? { motor_speed_mmps: motorSpeed, gear: motorGear }
          : channel === 'steering'
            ? {
                target_angle_raw: steerAngle,
                control_enable: true,
                alignment_enable: true,
              }
            : {
                pressure_request_raw: brakePressure,
                control_enable: true,
                alignment_enable: true,
                control_mode: 1,
              }
      const r = await api.controlDirect({
        channel,
        enabled: true,
        values,
        period_ms: channel === 'motor' ? 10 : 20,
      })
      setLastCtrl(r.control)
      const ch = (r.control?.direct_channels as string[] | undefined) || []
      setActive({
        motor: ch.includes('motor'),
        steering: ch.includes('steering'),
        brake: ch.includes('brake'),
      })
      setLog(
        `Low TX ${channel} · method=${String(r.control.method)} · active=${String(r.control.active)} · channels=${JSON.stringify(ch)} · safety bypass ON`,
      )
      await refresh()
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function stop(channel: 'motor' | 'steering' | 'brake') {
    setBusy(true)
    try {
      const r = await api.controlDirect({ channel, enabled: false })
      setLastCtrl(r.control)
      const ch = (r.control?.direct_channels as string[] | undefined) || []
      setActive({
        motor: ch.includes('motor'),
        steering: ch.includes('steering'),
        brake: ch.includes('brake'),
      })
      setLog(`Stopped low TX ${channel} · remaining=${JSON.stringify(ch)}`)
      await refresh()
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  const locked = busy || !!disabled
  const channels = (lastCtrl?.direct_channels as string[] | undefined) || []

  return (
    <div className="direct-grid" data-testid="direct-grid">
      <p className="control-callout" data-testid="direct-safety-banner" style={{ gridColumn: '1 / -1' }}>
        {motorOnly ? (
          <><strong>MTR-only route.</strong> Streams only <span className="mono">RT_DRIVE_CMD 0x204</span> on Low CAN.</>
        ) : (
          <><strong>Safety bypass ON</strong> for toolkit Low-bus unit tests: SES/SEB <span className="mono">control_enable</span> + <span className="mono">alignment_enable</span> are forced true on every TX.</>
        )}{' '}Watch <strong>TX</strong> lines below; FBK only appears if a peer/ECU answers.
      </p>
      <p className="muted small mono" data-testid="direct-channels-live" style={{ gridColumn: '1 / -1' }}>
        Backend direct channels: {channels.length ? channels.join(', ') : 'none'} · method{' '}
        {String(lastCtrl?.method ?? '—')}
      </p>

      <div className={`direct-card${active.motor ? ' streaming' : ''}`} data-testid="direct-motor">
        <div className="direct-card-head">
          <h3>Motor · Low · 0x204</h3>
          <span className={`chip tiny ${active.motor ? 'ok' : ''}`}>
            {active.motor ? 'streaming' : 'idle'}
          </span>
        </div>
        <p className="muted small">
          <span className="mono">RT_DRIVE_CMD</span> — not Host 0x300
        </p>
        <label className="field">
          <span className="field-label">Speed, mm/s</span>
          <NumericDraft testId="direct-motor-speed" value={motorSpeed} min={-500} max={3000} disabled={locked} onValue={setMotorSpeed} />
        </label>
        <label className="field">
          <span className="field-label">Gear</span>
          <select
            data-testid="direct-motor-gear"
            value={motorGear}
            disabled={locked}
            onChange={(e) => setMotorGear(Number(e.target.value))}
          >
            <option value={0}>0 = N</option>
            <option value={1}>1 = D</option>
            <option value={2}>2 = S</option>
            <option value={3}>3 = R</option>
          </select>
        </label>
        <div className="tx-line mono small" data-testid="direct-motor-tx">
          TX 0x204 · speed={signalText(txMotor, 'motor_speed_mmps')} · gear=
          {signalText(txMotor, 'gear')} · {txMotor?.freshness ?? 'no frame yet'}
        </div>
        <div className="fbk-line muted small mono">
          FBK 0x206 · {signalText(fbkMtr, 'actual_speed_mmps') || '—'} · gear{' '}
          {signalText(fbkMtr, 'gear_state') || signalText(fbkMtr, 'gear') || '—'}
        </div>
        <div className="actions tight">
          <button
            type="button"
            className="primary"
            data-testid="btn-direct-motor-start"
            disabled={locked}
            onClick={() => void start('motor')}
          >
            {active.motor ? 'Update stream' : 'Start stream'}
          </button>
          <button
            type="button"
            className="secondary"
            data-testid="btn-direct-motor-stop"
            disabled={locked || !active.motor}
            onClick={() => void stop('motor')}
          >
            Stop
          </button>
        </div>
      </div>

      {!motorOnly && <div className={`direct-card${active.steering ? ' streaming' : ''}`} data-testid="direct-steering">
        <div className="direct-card-head">
          <h3>Steering · Low · 0x169</h3>
          <span className={`chip tiny ${active.steering ? 'ok' : ''}`}>
            {active.steering ? 'streaming' : 'idle'}
          </span>
        </div>
        <p className="muted small">
          <span className="mono">VCU_SES_REQ</span> · safety bypass forced ON
        </p>
        <label className="field">
          <span className="field-label">Target angle raw (0.1°)</span>
          <NumericDraft testId="direct-steer-angle" value={steerAngle} min={-450} max={450} disabled={locked} onValue={setSteerAngle} />
          <span className="field-hint">±450 · control_enable=1 · alignment_enable=1</span>
        </label>
        <div className="tx-line mono small" data-testid="direct-steer-tx">
          TX 0x169 · angle={signalText(txSteer, 'target_angle_raw')} · en=
          {signalText(txSteer, 'control_enable')}/{signalText(txSteer, 'alignment_enable')} ·{' '}
          {txSteer?.freshness ?? 'no frame yet'}
        </div>
        <div className="fbk-line muted small mono">
          FBK SES · {signalText(fbkSes, 'angle_deg') || signalText(fbkSes, 'target_angle_raw') || '—'}
        </div>
        <div className="actions tight">
          <button
            type="button"
            className="primary"
            data-testid="btn-direct-steer-start"
            disabled={locked}
            onClick={() => void start('steering')}
          >
            {active.steering ? 'Update stream' : 'Start stream'}
          </button>
          <button
            type="button"
            className="secondary"
            data-testid="btn-direct-steer-stop"
            disabled={locked || !active.steering}
            onClick={() => void stop('steering')}
          >
            Stop
          </button>
        </div>
      </div>}

      {!motorOnly && <div className={`direct-card${active.brake ? ' streaming' : ''}`} data-testid="direct-brake">
        <div className="direct-card-head">
          <h3>Brake · Low · 0x7B9</h3>
          <span className={`chip tiny ${active.brake ? 'ok' : ''}`}>
            {active.brake ? 'streaming' : 'idle'}
          </span>
        </div>
        <p className="muted small">
          <span className="mono">VCU_SEB_REQ</span> · safety bypass forced ON
        </p>
        <label className="field">
          <span className="field-label">Pressure request raw 0–100</span>
          <NumericDraft testId="direct-brake-pressure" value={brakePressure} min={0} max={100} disabled={locked} onValue={setBrakePressure} />
          <span className="field-hint">control_enable=1 · alignment_enable=1</span>
        </label>
        <div className="tx-line mono small" data-testid="direct-brake-tx">
          TX 0x7B9 · pressure={signalText(txBrake, 'pressure_request_raw')} · en=
          {signalText(txBrake, 'control_enable')}/{signalText(txBrake, 'alignment_enable')} ·{' '}
          {txBrake?.freshness ?? 'no frame yet'}
        </div>
        <div className="fbk-line muted small mono">
          FBK SEB · {signalText(fbkSeb, 'pressure_kpa') || signalText(fbkSeb, 'status') || '—'}
        </div>
        <div className="actions tight">
          <button
            type="button"
            className="primary"
            data-testid="btn-direct-brake-start"
            disabled={locked}
            onClick={() => void start('brake')}
          >
            {active.brake ? 'Update stream' : 'Start stream'}
          </button>
          <button
            type="button"
            className="secondary"
            data-testid="btn-direct-brake-stop"
            disabled={locked || !active.brake}
            onClick={() => void stop('brake')}
          >
            Stop
          </button>
        </div>
      </div>}
    </div>
  )
}

export function Control() {
  const setStatus = useAppStore((s) => s.setStatus)
  const status = useAppStore((s) => s.status)
  const method = useAppStore((s) => s.controlMethod)
  const setMethod = useAppStore((s) => s.setControlMethod)
  const [log, setLog] = useState('')
  const [busy, setBusy] = useState(false)
  const [speed, setSpeed] = useState(500)
  const [yaw, setYaw] = useState(250)
  const [gear, setGear] = useState(1) // firmware: 0=N 1=D 2=S 3=R
  const [periodMs, setPeriodMs] = useState(100)
  const [periodic, setPeriodic] = useState(true)
  const [leaseId, setLeaseId] = useState<string | null>(null)
  const leaseRef = useRef<string | null>(null)
  leaseRef.current = leaseId
  const [kbEnabled, setKbEnabled] = useState(false)
  const [kbSnap, setKbSnap] = useState<Record<string, unknown> | null>(null)
  const [ctrlStatus, setCtrlStatus] = useState<Record<string, unknown> | null>(null)
  const seqRef = useRef(0)
  const keysRef = useRef<Record<string, boolean>>({})
  const kbEnabledRef = useRef(false)
  kbEnabledRef.current = kbEnabled

  // Clear control intent / lease on unmount (architecture safety invariant).
  useEffect(() => {
    return () => {
      const st = useAppStore.getState().status
      const lid = leaseRef.current
      if (lid && st?.session?.session_id) {
        void api.releaseLease(st.session.session_id, lid).catch(() => undefined)
      }
      void api.stopAnalysis().catch(() => undefined)
      void api.controlRelease('unmount').catch(() => undefined)
    }
  }, [])

  // Poll control status so method badge stays accurate.
  useEffect(() => {
    const id = window.setInterval(() => {
      void api
        .controlStatus()
        .then((r) => setCtrlStatus(r.control))
        .catch(() => undefined)
    }, 1000)
    return () => window.clearInterval(id)
  }, [])

  // Keyboard teleop → backend /control/intent (high-bus Host kinematics only)
  useEffect(() => {
    if (!kbEnabled || method !== 'high') return
    const onDown = (e: KeyboardEvent) => {
      keysRef.current[e.code] = true
      if (['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Space'].includes(e.code)) {
        e.preventDefault()
      }
    }
    const onUp = (e: KeyboardEvent) => {
      keysRef.current[e.code] = false
    }
    const onBlur = () => {
      keysRef.current = {}
      void api.controlRelease('blur').catch(() => undefined)
      setKbEnabled(false)
    }
    const onVis = () => {
      if (document.hidden) {
        void api.controlRelease('tab_hidden').catch(() => undefined)
        setKbEnabled(false)
      }
    }
    window.addEventListener('keydown', onDown)
    window.addEventListener('keyup', onUp)
    window.addEventListener('blur', onBlur)
    document.addEventListener('visibilitychange', onVis)

    const tick = window.setInterval(() => {
      if (!kbEnabledRef.current) return
      const k = keysRef.current
      let throttle = 0
      let steer = 0
      if (k.KeyW || k.ArrowUp) throttle += 1
      if (k.KeyS || k.ArrowDown) throttle -= 1
      if (k.KeyA || k.ArrowLeft) steer -= 1
      if (k.KeyD || k.ArrowRight) steer += 1
      const hard_brake = !!k.ShiftLeft || !!k.ShiftRight
      const estop = !!k.Space
      seqRef.current += 1
      void api
        .controlIntent({
          sequence: seqRef.current,
          source: 'control_keyboard',
          mode: 'kinematics',
          throttle,
          steer,
          gear: throttle < 0 ? 3 : throttle > 0 ? 1 : 1,
          hard_brake,
          estop,
        })
        .then((r) => {
          setKbSnap(r.control)
          setCtrlStatus(r.control)
        })
        .catch((e) => {
          const msg = String(e)
          // Only stale-sequence races are ignorable; TX/session conflicts mean control lost.
          if (isStaleSequenceError(msg)) return
          setKbEnabled(false)
          setLog(`Control lost: ${msg}`)
          void api.status().then(setStatus).catch(() => undefined)
        })
    }, 50)

    return () => {
      window.removeEventListener('keydown', onDown)
      window.removeEventListener('keyup', onUp)
      window.removeEventListener('blur', onBlur)
      document.removeEventListener('visibilitychange', onVis)
      window.clearInterval(tick)
    }
  }, [kbEnabled, method])

  const refresh = useCallback(async () => {
    const st = await api.status()
    setStatus(st)
    try {
      const c = await api.controlStatus()
      setCtrlStatus(c.control)
    } catch {
      /* ignore */
    }
    return st
  }, [setStatus])

  async function ensureSessionReady() {
    const st = await refresh()
    if (!st.session?.session_id) {
      throw new Error('No active session. Start Computer or connect Real in Settings first.')
    }
    return st
  }

  /** Switch exclusive control method — release the other path first. */
  async function selectMethod(next: ControlMethod) {
    if (next === method) return
    setBusy(true)
    try {
      setKbEnabled(false)
      const clean = await cleanupControlStreams('method_switch')
      setLeaseId(null)
      setMethod(next)
      setLog(
        (next === 'high'
          ? 'Method: High bus · Host kinematics (HOST_DRIVE_CMD 0x300)'
          : next === 'low'
            ? 'Method: Low bus · Direct actuators (motor / steer / brake)'
            : next === 'mtr'
              ? 'Method: Direct MTR · Low bus motor only (RT_DRIVE_CMD 0x204)'
            : 'Method: HMI (mode/power requests only — not motion)') +
          (clean.ok ? '' : ` · ${clean.detail}`),
      )
      await refresh()
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function enableTx() {
    setBusy(true)
    try {
      let st = await ensureSessionReady()
      if (String(st.session.bench_tx).toLowerCase() !== 'enabled') {
        await api.setBenchTx(st.session.session_id!, true, st.session.revision)
        st = await refresh()
      }
      setLog('Bench TX armed (explicit)')
      setStatus(st)
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function disableTx() {
    setBusy(true)
    try {
      const st = await refresh()
      const sid = st.session?.session_id
      if (!sid) {
        setLog('No session — Bench TX already off')
        return
      }
      if (String(st.session?.bench_tx ?? '').toLowerCase() !== 'enabled') {
        setLog('Bench TX already disabled')
        return
      }
      setKbEnabled(false)
      await api.controlRelease('bench_tx_off').catch(() => undefined)
      await api.setBenchTx(sid, false, st.session.revision)
      setLog('Bench TX disabled — inject / keyboard TX gated off')
      await refresh()
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function injectHostDrive() {
    setBusy(true)
    try {
      const st = await ensureSessionReady()
      if (String(st.session.bench_tx).toLowerCase() !== 'enabled') {
        throw new Error('Bench TX is off. Arm TX explicitly before HostDrive inject.')
      }
      setKbEnabled(false)
      const clean = await cleanupControlStreams('pre_inject')
      const res = await api.hostDrive({
        speed_mmps: speed,
        yaw_rate_mrad_s: yaw,
        gear,
        period_ms: periodic ? periodMs : null,
      })
      const lid = (res as { lease_id?: string }).lease_id
      if (typeof lid === 'string') setLeaseId(lid)
      setLog(
        `High-bus inject HOST_DRIVE_CMD: ${JSON.stringify(res)}` +
          (clean.ok ? '' : ` · ${clean.detail}`),
      )
      await refresh()
      await useActiveTxStore.getState().refreshJobs()
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function stopHostDriveInject() {
    setBusy(true)
    try {
      const result = await api.stopAnalysis()
      setLeaseId(null)
      setLog(`Stopped periodic HostDrive analysis (${result.stopped} job(s))`)
      await refresh()
      await useActiveTxStore.getState().refreshJobs()
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function stopAll() {
    setBusy(true)
    try {
      const st = await refresh()
      if (st.session?.session_id) {
        await api.stopAll(st.session.session_id, st.session.revision)
      }
      await api.stopAnalysis().catch(() => undefined)
      setKbEnabled(false)
      await useActiveTxStore.getState().stopAll().catch(() => undefined)
      setLeaseId(null)
      setLog('Stop All — high and low motion streams cleared')
      await refresh()
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function setMode(mode: string) {
    setBusy(true)
    try {
      const st = await ensureSessionReady()
      await api.vehicleView(st.session.session_id!, { requested_mode: mode })
      // Protocol HMI_MODE_REQ is MANUAL=0 / AUTO=1 only; PURE_SIM is UI request label.
      if (mode === 'MANUAL' || mode === 'AUTO') {
        const res = await api.hmiMode(mode === 'AUTO' ? 1 : 0, true)
        setLog(`HMI mode TX: ${JSON.stringify(res)} (confirmed remains independent)`)
      } else {
        setLog(`Requested mode: ${mode} (no wire HMI_MODE_REQ for PURE_SIM yet)`)
      }
      await refresh()
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function setPower(power: string) {
    setBusy(true)
    try {
      const st = await ensureSessionReady()
      await api.vehicleView(st.session.session_id!, { requested_power: power })
      const res = await api.hmiPower(power === 'ON' ? 1 : 0, true)
      setLog(`HMI power TX: ${JSON.stringify(res)}`)
      await refresh()
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function disableHmi(kind: 'mode' | 'power') {
    setBusy(true)
    try {
      const result = kind === 'mode' ? await api.hmiMode(0, false) : await api.hmiPower(0, false)
      setLog(`HMI ${kind} periodic request disabled: ${JSON.stringify(result)}`)
      await refresh()
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  const activeMethod = String(ctrlStatus?.method ?? 'none')
  const activeLabel = String(ctrlStatus?.method_label ?? 'No active motion method')
  const benchOn = String(status?.session?.bench_tx ?? '').toLowerCase() === 'enabled'
  const sessionId = status?.session?.session_id
  const activeProfile = status?.session?.profile ?? status?.profile ?? 'pure_software'
  const fullVehicle = activeProfile === 'full_vehicle'
  const gearLabels: Record<number, string> = { 0: 'N', 1: 'D', 2: 'S', 3: 'R' }

  useEffect(() => {
    if (fullVehicle && method !== 'high') {
      setKbEnabled(false)
      setMethod('high')
      setLog('Full Vehicle exposes named High-bus injection only. Bench bypass and HMI controls are hidden.')
    }
  }, [fullVehicle, method])

  return (
    <WorkspaceShell
      testId="workspace-control"
      className="control-workspace"
      title="Control"
      description={<>Select one motion method at a time. High, Low-all and MTR-only are exclusive because High control also makes RT publish Low <span className="mono">0x204</span>. All methods share the same Bench TX Arm gate. Quick keyboard / TX / fake-signal tools live in the Control activity sidebar.</>}
    >

      <div className="control-setup-grid">
      {/* ── Session gate ─────────────────────────────────────────── */}
      <section className="panel control-session-panel" data-testid="control-session-panel">
        <div className="control-status-row" data-testid="control-status-row">
          <div className="control-status-item">
            <span className="muted small">Session</span>
            <strong className="mono" data-testid="control-session-id">
              {sessionId ?? 'none'}
            </strong>
          </div>
          <div className="control-status-item">
            <span className="muted small">Bench TX (TX gate)</span>
            <strong
              className={benchOn ? 'ok-text' : 'danger-text'}
              data-testid="control-bench-tx"
            >
              {benchOn ? 'ON — bus TX allowed' : 'OFF — inject/control blocked'}
            </strong>
          </div>
          <div className="control-status-item">
            <span className="muted small">Backend motion</span>
            <strong className="mono" data-testid="control-active-method">
              {activeMethod}
            </strong>
            <span className="muted small">{activeLabel}</span>
          </div>
        </div>
        {!benchOn && (
          <p className="control-callout" data-testid="control-bench-hint">
            Turn <strong>Bench TX ON</strong> before keyboard, inject, or low-bus streams.
            This is a safety gate, not a motion command.
          </p>
        )}
        <div className="actions tight">
          {!sessionId ? (
            <p className="control-callout" data-testid="btn-open-settings-session">
              No session — start one from <strong>Settings</strong> in Workspace explorer.
            </p>
          ) : benchOn ? (
            <>
              <button
                type="button"
                className="secondary"
                data-testid="btn-disable-tx"
                disabled={busy}
                onClick={() => void disableTx()}
              >
                Turn Bench TX off
              </button>
              <button
                type="button"
                className="secondary"
                data-testid="btn-enable-tx"
                disabled={busy}
                title="Session already enabled — re-assert gate"
                onClick={() => void enableTx()}
              >
                Keep Bench TX on
              </button>
            </>
          ) : (
            <button
              type="button"
              className="primary"
              data-testid="btn-enable-tx"
              disabled={busy}
              onClick={() => void enableTx()}
            >
              Turn Bench TX on
            </button>
          )}
          <button
            type="button"
            className="danger"
            data-testid="btn-stop-all"
            disabled={busy}
            title="Stop inject jobs, keyboard intent, and direct streams"
            onClick={() => void stopAll()}
          >
            Stop all motion TX
          </button>
        </div>
      </section>

      {/* ── Method picker ────────────────────────────────────────── */}
      <section className="panel control-method-panel" data-testid="control-method-picker">
        <h2>What do you want to control?</h2>
        <div className="seg control-method-seg" role="tablist" aria-label="Control method">
          <button
            type="button"
            role="tab"
            data-testid="control-method-high"
            className={method === 'high' ? 'seg-btn active' : 'seg-btn'}
            aria-selected={method === 'high'}
            disabled={busy}
            onClick={() => void selectMethod('high')}
          >
            High bus · Host drive
          </button>
          {!fullVehicle && <button
            type="button"
            role="tab"
            data-testid="control-method-low"
            className={method === 'low' ? 'seg-btn active' : 'seg-btn'}
            aria-selected={method === 'low'}
            disabled={busy}
            onClick={() => void selectMethod('low')}
          >
            Low bus · Actuators
          </button>}
          {!fullVehicle && <button
            type="button"
            role="tab"
            data-testid="control-method-mtr"
            className={method === 'mtr' ? 'seg-btn active' : 'seg-btn'}
            aria-selected={method === 'mtr'}
            disabled={busy}
            onClick={() => void selectMethod('mtr')}
          >
            MTR direct · 0x204
          </button>}
          {!fullVehicle && <button
            type="button"
            role="tab"
            data-testid="control-method-hmi"
            className={method === 'hmi' ? 'seg-btn active' : 'seg-btn'}
            aria-selected={method === 'hmi'}
            disabled={busy}
            onClick={() => void selectMethod('hmi')}
          >
            HMI · Mode / power
          </button>}
        </div>
        <div className="method-compare" data-testid="method-compare">
          {method === 'high' && (
            <div className="method-card active" data-testid="method-blurb-high">
              <strong>High bus · Host kinematics</strong>
              <p className="muted small" style={{ margin: '6px 0 0' }}>
                You send <span className="mono">HOST_DRIVE_CMD 0x300</span> (speed / yaw /
                gear). RT runs kinematics. Use keyboard here or the Drive tab. Does{' '}
                <strong>not</strong> talk to motor/steer/brake IDs on Low.
              </p>
            </div>
          )}
          {method === 'low' && (
            <div className="method-card active" data-testid="method-blurb-low">
              <strong>Low bus · Direct actuators</strong>
              <p className="muted small" style={{ margin: '6px 0 0' }}>
                Streams motor <span className="mono">0x204</span>, steer{' '}
                <span className="mono">0x169</span>, brake <span className="mono">0x7B9</span>{' '}
                for unit tests. Starting any channel stops high-bus Host drive jobs.
              </p>
            </div>
          )}
          {method === 'mtr' && (
            <div className="method-card active" data-testid="method-blurb-mtr">
              <strong>MTR direct · Low CAN motor only</strong>
              <p className="muted small" style={{ margin: '6px 0 0' }}>
                Streams <span className="mono">RT_DRIVE_CMD 0x204</span> directly to the
                MTR contract without High-bus host kinematics. Selecting this route first
                releases High and other Low actuator streams.
              </p>
            </div>
          )}
          {method === 'hmi' && (
            <div className="method-card active" data-testid="method-blurb-hmi">
              <strong>HMI · Mode / power only</strong>
              <p className="muted small" style={{ margin: '6px 0 0' }}>
                Sends HMI request frames (and vehicle-view labels). Not a drive method —
                no throttle/yaw. Requested vs confirmed stay separate until ECU feedback.
              </p>
            </div>
          )}
        </div>
      </section>
      </div>

      {method === 'high' && (
        <div className="control-high-grid">
          {!fullVehicle && <section className="panel" data-testid="keyboard-control">
            <h2>1 · Keyboard teleop</h2>
            <p className="muted small">
              Continuous Host intent via <span className="mono">POST /control/intent</span>{' '}
              (shaped on backend). Needs Bench TX on. Focus the page; blur / hide tab
              releases control.
            </p>
            <div className="actions">
              <button
                type="button"
                data-testid="btn-kb-enable"
                disabled={busy}
                className={kbEnabled ? 'secondary' : 'primary'}
                onClick={() => {
                  if (kbEnabled) {
                    setKbEnabled(false)
                    setBusy(true)
                    void api.controlRelease('disable')
                      .then(async () => {
                        setLog('Keyboard control off')
                        await refresh()
                      })
                      .catch((e) => setLog(String(e)))
                      .finally(() => setBusy(false))
                  } else {
                    void ensureSessionReady()
                      .then(async () => {
                        // Own the intent stream: stop low-direct / Drive / inject jobs.
                        await api.controlRelease('kb_enable').catch(() => undefined)
                        for (const ch of ['motor', 'steering', 'brake'] as const) {
                          await api
                            .controlDirect({ channel: ch, enabled: false })
                            .catch(() => undefined)
                        }
                        await api.stopAnalysis().catch(() => undefined)
                        seqRef.current = 0
                        setKbEnabled(true)
                        setLog('Keyboard control on — WASD / arrows → /control/intent')
                      })
                      .catch((e) => setLog(String(e)))
                  }
                }}
              >
                {kbEnabled ? 'Stop keyboard' : 'Start keyboard'}
              </button>
            </div>
            <ul className="controls-legend muted small">
              <li>
                <kbd>W</kbd>/<kbd>↑</kbd> throttle · <kbd>S</kbd>/<kbd>↓</kbd> reverse
              </li>
              <li>
                <kbd>A</kbd>/<kbd>D</kbd> yaw · <kbd>Shift</kbd> hard brake · <kbd>Space</kbd>{' '}
                ESTOP
              </li>
            </ul>
            {kbEnabled && (
              <p className="ok-text small" data-testid="kb-active-banner">
                Keyboard armed — keys stream Host intent on High bus.
              </p>
            )}
            {kbSnap && (
              <dl className="kv compact" data-testid="kb-shaped">
                <dt>Shaped speed</dt>
                <dd className="mono">{String(kbSnap.shaped_speed_mmps)} mm/s</dd>
                <dt>Shaped yaw</dt>
                <dd className="mono">{String(kbSnap.shaped_yaw_mrad_s)} mrad/s</dd>
                <dt>Gear</dt>
                <dd className="mono">
                  {String(kbSnap.gear_label)} ({String(kbSnap.gear)})
                </dd>
                <dt>Active</dt>
                <dd>{kbSnap.active ? 'yes' : 'no'}</dd>
              </dl>
            )}
          </section>}

          <section className="panel" data-testid="high-analysis-inject">
            <h2>2 · Numeric inject (analysis)</h2>
            <p className="muted small">
              One-shot or periodic <span className="mono">HOST_DRIVE_CMD</span> via{' '}
              <span className="mono">POST /analysis/host-drive</span>. For fixed
              speed/yaw experiments — not the same as keyboard (keyboard uses
              /control/intent).
            </p>
            <div className="form-grid">
              <label>
                Speed, mm/s
                <NumericDraft testId="input-speed" value={speed} min={-500} max={3000} onValue={setSpeed} />
                <span className="field-hint">−500 … 3000</span>
              </label>
              <label>
                Yaw rate, mrad/s
                <NumericDraft testId="input-yaw" value={yaw} min={-3000} max={3000} onValue={setYaw} />
                <span className="field-hint">−3000 … 3000</span>
              </label>
              <label>
                Gear
                <select
                  data-testid="input-gear"
                  value={gear}
                  onChange={(e) => setGear(Number(e.target.value))}
                >
                  {([0, 1, 2, 3] as const).map((g) => (
                    <option key={g} value={g}>
                      {g} = {gearLabels[g]}
                    </option>
                  ))}
                </select>
              </label>
              <label>
                Period, ms
                <NumericDraft testId="input-period" value={periodMs} min={10} disabled={!periodic} onValue={setPeriodMs} />
                <span className="field-hint">Only used if periodic is checked</span>
              </label>
              <label className="check">
                <input
                  data-testid="check-periodic"
                  type="checkbox"
                  checked={periodic}
                  onChange={(e) => setPeriodic(e.target.checked)}
                />
                Periodic stream (else one-shot frame)
              </label>
            </div>
            <div className="actions">
              <button
                type="button"
                className="primary"
                data-testid="btn-inject-drive"
                disabled={busy}
                onClick={() => void injectHostDrive()}
              >
                {periodic ? 'Start periodic inject' : 'Send one-shot inject'}
              </button>
              <button
                type="button"
                className="secondary"
                data-testid="btn-stop-drive-inject"
                disabled={busy || (!periodic && !leaseId)}
                onClick={() => void stopHostDriveInject()}
              >
                Stop periodic inject
              </button>
            </div>
          </section>
        </div>
      )}

      {method === 'low' && (
        <section className="panel" data-testid="direct-actuators">
          <h2>Low bus · Direct actuators</h2>
          <p className="muted small">
            Unit-test path. Each Start enables a continuous Low-bus job (needs Bench TX).
            Starting any channel preempts high Host kinematics.
          </p>
          <DirectActuatorCards
            busy={busy}
            setBusy={setBusy}
            setLog={setLog}
            ensureSessionReady={ensureSessionReady}
            refresh={refresh}
          />
        </section>
      )}

      {method === 'mtr' && (
        <section className="panel" data-testid="direct-mtr">
          <h2>MTR direct · Low bus</h2>
          <p className="muted small">
            Motor-only unit-test route. Uses the shared Bench TX Arm gate and backend
            watchdog; leaving this workspace stops the stream.
          </p>
          <DirectActuatorCards
            busy={busy}
            setBusy={setBusy}
            setLog={setLog}
            ensureSessionReady={ensureSessionReady}
            refresh={refresh}
            motorOnly
          />
        </section>
      )}

      {method === 'hmi' && (
        <section className="panel" data-testid="hmi-panel">
          <h2>HMI · Mode and power requests</h2>
          <p className="muted small">
            Wire: MANUAL/AUTO mode and ON/OFF power. PURE_SIM is UI-only (no HMI_MODE_REQ
            enum yet). Needs Bench TX for bus TX.
          </p>
          <div className="hmi-request-block">
            <span className="field-label">Mode request</span>
            <div className="actions tight">
              {(['MANUAL', 'AUTO', 'PURE_SIM'] as const).map((m) => (
                <button
                  key={m}
                  type="button"
                  data-testid={`btn-mode-${m.toLowerCase()}`}
                  disabled={busy}
                  className={
                    status?.session?.requested_mode === m ? 'primary' : 'secondary'
                  }
                  onClick={() => void setMode(m)}
                >
                  {m === 'PURE_SIM' ? 'PURE_SIM (UI only)' : m}
                </button>
              ))}
              <button
                type="button"
                className="secondary"
                data-testid="btn-mode-disable"
                disabled={busy}
                onClick={() => void disableHmi('mode')}
              >
                Disable mode TX
              </button>
            </div>
          </div>
          <div className="hmi-request-block">
            <span className="field-label">Power request</span>
            <div className="actions tight">
              <button
                type="button"
                data-testid="btn-power-on"
                disabled={busy}
                className={status?.session?.requested_power === 'ON' ? 'primary' : 'secondary'}
                onClick={() => void setPower('ON')}
              >
                ON
              </button>
              <button
                type="button"
                data-testid="btn-power-off"
                disabled={busy}
                className={status?.session?.requested_power === 'OFF' ? 'primary' : 'secondary'}
                onClick={() => void setPower('OFF')}
              >
                OFF
              </button>
              <button
                type="button"
                className="secondary"
                data-testid="btn-power-disable"
                disabled={busy}
                onClick={() => void disableHmi('power')}
              >
                Disable power TX
              </button>
            </div>
          </div>
          <dl className="kv compact" data-testid="hmi-requested-confirmed">
            <dt>Mode requested / confirmed</dt>
            <dd className="mono">
              {status?.session?.requested_mode ?? '—'} /{' '}
              {status?.session?.confirmed_mode ?? '—'}
            </dd>
            <dt>Power requested / confirmed</dt>
            <dd className="mono">
              {status?.session?.requested_power ?? '—'} /{' '}
              {status?.session?.confirmed_power ?? '—'}
            </dd>
          </dl>
        </section>
      )}

      <pre className="log" data-testid="control-log">
        {log ||
          '1) Turn Bench TX on  ·  2) Pick High / Low / HMI  ·  3) Start keyboard, inject, or streams'}
      </pre>
    </WorkspaceShell>
  )
}
