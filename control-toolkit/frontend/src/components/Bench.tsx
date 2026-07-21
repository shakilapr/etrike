import { useCallback, useEffect, useState } from 'react'
import { api } from '../api'
import { useAppStore } from '../store'
import { WorkspaceShell } from './WorkspaceShell'

export function Bench() {
  const status = useAppStore((s) => s.status)
  const setStatus = useAppStore((s) => s.setStatus)
  const setWorkspace = useAppStore((s) => s.setWorkspace)
  const [available, setAvailable] = useState<Array<Record<string, unknown>>>([])
  const [running, setRunning] = useState<Array<Record<string, unknown>>>([])
  const [busy, setBusy] = useState(false)
  const [log, setLog] = useState('')
  const fullVehicle = (status?.session?.profile ?? status?.profile) === 'full_vehicle'

  const refreshPeers = useCallback(async () => {
    const peers = await api.syntheticPeers()
    setAvailable(peers.available || [])
    setRunning(peers.running || [])
  }, [])

  useEffect(() => {
    void refreshPeers().catch((e) => setLog(String(e)))
  }, [refreshPeers])

  async function ensureBenchTx() {
    let st = await api.status()
    if (!st.session?.session_id) {
      throw new Error('No active session. Start Computer or connect Real in Settings first.')
    }
    if (st.session.bench_tx !== 'enabled') {
      throw new Error(
        'Bench TX is off. Enable Bench TX explicitly before starting synthetic peers.',
      )
    }
    setStatus(st)
  }

  async function startHostStimulus() {
    setBusy(true)
    try {
      await ensureBenchTx()
      const { cleanupControlStreams } = await import('../lib/cleanup')
      const clean = await cleanupControlStreams('bench_synthetic_start', { direct: false })
      const result = await api.startSyntheticPeers(['host_drive_analysis'])
      await refreshPeers()
      setStatus(await api.status())
      setLog(
        `Started host_drive_analysis: ${JSON.stringify(result.started)}` +
          (clean.ok ? '' : ` · ${clean.detail}`),
      )
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function stopStimuli() {
    setBusy(true)
    try {
      const result = await api.stopSyntheticPeers()
      await refreshPeers()
      setStatus(await api.status())
      setLog(`Stopped ${result.stopped} analysis stimulus job(s)`)
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  const hostRunning = running.some((p) => String(p.name) === 'host_drive_analysis')
  return (
    <WorkspaceShell
      testId="workspace-bench"
      className="bench-workspace"
      title="Bench"
      description="Physical ECU under test and synthetic peers. Physical Bench TX is the safety gate for bus activity."
    >
      <div className="bench-grid">
      <section className="panel bench-setup-panel">
        <h2>Bench setup</h2>
        <ol className="setup-list">
          <li>Physical target ECU(s) — deferred (hardware track)</li>
          <li>Connected bus/channel — virtual high/low active</li>
          <li>Peers present — none (analysis inject only)</li>
          <li>Missing peers to emulate — not full synthetic vehicle</li>
          <li>
            Control path — High Host kinematics (0x300) or Low direct actuators (exclusive)
          </li>
          <li>Review periodic TX before enable</li>
        </ol>
        <dl className="kv">
          <dt>Active profile</dt>
          <dd>{status?.session?.profile ?? status?.profile ?? '—'}</dd>
          <dt>Destination</dt>
          <dd>{status?.session?.destination ?? '—'}</dd>
          <dt>Bench TX</dt>
          <dd>{status?.session?.bench_tx ?? 'disabled'}</dd>
          <dt>Leases</dt>
          <dd className="mono">
            {(status?.session?.leases || []).join(', ') || 'none'}
          </dd>
          <dt>Jobs</dt>
          <dd className="mono">{(status?.session?.jobs || []).join(', ') || 'none'}</dd>
        </dl>
      </section>
      {!fullVehicle ? <section className="panel" data-testid="synthetic-peers-panel">
        <h2>Analysis stimuli</h2>
        <p className="muted small">
          Explicit zero-speed HostDrive stimulus on High bus. This is not a synthetic
          vehicle or ECU heartbeat mesh; Bench TX remains the safety gate.
        </p>
        <dl className="kv compact">
          <dt>Available</dt>
          <dd className="mono">
            {available.map((p) => String(p.name)).join(', ') || 'none'}
          </dd>
          <dt>Running</dt>
          <dd className="mono" data-testid="synthetic-running">
            {running.map((p) => String(p.name)).join(', ') || 'none'}
          </dd>
        </dl>
        <div className="actions tight">
          <button
            type="button"
            data-testid="btn-synthetic-start"
            disabled={busy || hostRunning || available.length === 0}
            onClick={() => void startHostStimulus()}
          >
            Start zero-speed Host stimulus
          </button>
          <button
            type="button"
            className="secondary"
            data-testid="btn-synthetic-stop"
            disabled={busy || !hostRunning}
            onClick={() => void stopStimuli()}
          >
            Stop analysis stimuli
          </button>
          <button
            type="button"
            className="secondary"
            data-testid="btn-bench-open-control"
            onClick={() => setWorkspace('control')}
          >
            Open Control
          </button>
          <button
            type="button"
            className="secondary"
            data-testid="btn-bench-open-diagnostics"
            onClick={() => setWorkspace('diagnostics')}
          >
            Open Diagnostics
          </button>
        </div>
        <pre className="log" data-testid="synthetic-log">
          {log || 'No analysis stimulus action yet.'}
        </pre>
      </section> : (
        <section className="panel mode-restriction" data-testid="synthetic-peers-hidden">
          <h2>Bench stimuli unavailable</h2>
          <p className="muted">
            Full Vehicle observes physical ECUs and allows explicit named injection only.
            Switch to Real Bench or Computer to run synthetic stimuli.
          </p>
        </section>
      )}
      </div>
    </WorkspaceShell>
  )
}
