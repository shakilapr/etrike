import { useCallback, useState } from 'react'
import { useAppStore, type MessageState } from './store'
import { useBackendStream } from './useStream'
import { api } from './api'
import './App.css'

function FreshnessBadge({ value }: { value: string }) {
  return (
    <span className={`fresh fresh-${value.toLowerCase()}`} data-testid="freshness">
      {value}
    </span>
  )
}

function signalText(m: MessageState | undefined, key: string): string {
  if (!m?.signals?.[key]) return '—'
  const s = m.signals[key]
  return String(s.enum_label ?? s.engineering_value ?? '—')
}

function Topbar() {
  const status = useAppStore((s) => s.status)
  const quality = useAppStore((s) => s.streamQuality)
  const mismatch = useAppStore((s) => s.protocolMismatch)
  const high = status?.adapter?.channels?.high
  const low = status?.adapter?.channels?.low
  return (
    <header className="topbar" data-testid="topbar">
      <div className="brand">Control Toolkit</div>
      <div className="chip" data-testid="chip-profile">
        profile: {status?.session?.profile ?? status?.profile ?? '—'}
      </div>
      <div className="chip" data-testid="chip-adapter">
        adapter: {status?.adapter?.health ?? '—'}
      </div>
      <div className="chip" data-testid="chip-high">
        high: {high?.activity ?? '—'} ({high?.rx_count ?? 0})
      </div>
      <div className="chip" data-testid="chip-low">
        low: {low?.activity ?? '—'} ({low?.rx_count ?? 0})
      </div>
      <div className="chip" data-testid="chip-bench-tx">
        bench TX: {status?.session?.bench_tx ?? 'disabled'}
      </div>
      <div className={`chip quality-${quality}`} data-testid="chip-stream">
        stream: {quality.toUpperCase()}
      </div>
      {mismatch && (
        <div className="chip danger" data-testid="chip-mismatch">
          PROTOCOL MISMATCH
        </div>
      )}
      <div className="chip mono muted" data-testid="chip-hash">
        hash: {(status?.wire_hash ?? '').slice(0, 12)}…
      </div>
    </header>
  )
}

function Sidebar() {
  const workspace = useAppStore((s) => s.workspace)
  const setWorkspace = useAppStore((s) => s.setWorkspace)
  return (
    <nav className="sidebar" data-testid="sidebar">
      {(['overview', 'live', 'control'] as const).map((w) => (
        <button
          key={w}
          type="button"
          data-testid={`nav-${w}`}
          className={workspace === w ? 'nav active' : 'nav'}
          onClick={() => setWorkspace(w)}
        >
          {w}
        </button>
      ))}
    </nav>
  )
}

function Overview() {
  const messages = useAppStore((s) => s.messages)
  const status = useAppStore((s) => s.status)
  const drive = messages.find((m) => m.name === 'HOST_DRIVE_CMD')
  return (
    <div className="workspace" data-testid="workspace-overview">
      <h1>Overview</h1>
      <p className="muted">
        Analysis mode (safety-bypass style): inject only signals under study — not a full
        synthetic vehicle. Session {status?.session?.session_id ?? 'none'} · frames{' '}
        {messages.length}
      </p>
      <div className="cards">
        <div className="card" data-testid="card-yaw">
          <div className="card-title">Yaw rate (HOST_DRIVE_CMD)</div>
          {drive ? <FreshnessBadge value={drive.freshness} /> : null}
          <div className="metric" data-testid="metric-yaw">
            {signalText(drive, 'yaw_rate_mrad_s')}
            <span className="unit"> mrad/s</span>
          </div>
        </div>
        <div className="card" data-testid="card-speed">
          <div className="card-title">Speed (HOST_DRIVE_CMD)</div>
          {drive ? <FreshnessBadge value={drive.freshness} /> : null}
          <div className="metric" data-testid="metric-speed">
            {signalText(drive, 'speed_mmps')}
            <span className="unit"> mm/s</span>
          </div>
        </div>
        <div className="card" data-testid="card-gear">
          <div className="card-title">Gear</div>
          {drive ? <FreshnessBadge value={drive.freshness} /> : null}
          <div className="metric" data-testid="metric-gear">
            {signalText(drive, 'gear')}
          </div>
        </div>
        <div className="card" data-testid="card-ready">
          <div className="card-title">Backend</div>
          <div className="metric">{status?.ready ? 'ready' : 'not ready'}</div>
          <div className="mono muted">{status?.adapter?.health ?? '—'}</div>
        </div>
      </div>
    </div>
  )
}

function LiveCan() {
  const messages = useAppStore((s) => s.messages)
  const sorted = [...messages].sort(
    (a, b) => a.bus.localeCompare(b.bus) || a.can_id - b.can_id,
  )
  return (
    <div className="workspace" data-testid="workspace-live">
      <h1>Live CAN</h1>
      <table className="can-table" data-testid="live-can-table">
        <thead>
          <tr>
            <th>Fresh</th>
            <th>Bus</th>
            <th>ID</th>
            <th>Name</th>
            <th>Valid</th>
            <th>Signals</th>
          </tr>
        </thead>
        <tbody>
          {sorted.map((m) => (
            <tr key={`${m.bus}-${m.can_id}`} data-testid={`row-${m.bus}-${m.can_id}`}>
              <td>
                <FreshnessBadge value={m.freshness} />
              </td>
              <td>{m.bus}</td>
              <td className="mono">0x{m.can_id.toString(16).toUpperCase()}</td>
              <td>{m.name}</td>
              <td>{m.validation_status}</td>
              <td className="signals-cell">
                {Object.entries(m.signals || {})
                  .map(([k, v]) => `${k}=${v.enum_label ?? v.engineering_value}`)
                  .join(' · ')}
              </td>
            </tr>
          ))}
          {sorted.length === 0 && (
            <tr>
              <td colSpan={6} className="muted">
                No frames yet — use Control to enable Bench TX and inject host drive (yaw/speed).
              </td>
            </tr>
          )}
        </tbody>
      </table>
    </div>
  )
}

function Control() {
  const setStatus = useAppStore((s) => s.setStatus)
  const [log, setLog] = useState('')
  const [busy, setBusy] = useState(false)
  const [speed, setSpeed] = useState(500)
  const [yaw, setYaw] = useState(250)
  const [gear, setGear] = useState(1)
  const [periodMs, setPeriodMs] = useState(100)
  const [periodic, setPeriodic] = useState(true)

  const refresh = useCallback(async () => {
    const st = await api.status()
    setStatus(st)
    return st
  }, [setStatus])

  async function ensureSessionReady() {
    let st = await refresh()
    if (!st.session?.session_id) {
      await api.createSession('pure_software')
      st = await refresh()
    }
    if (st.session.bench_tx !== 'enabled') {
      await api.setBenchTx(st.session.session_id!, true, st.session.revision)
      st = await refresh()
    }
    return st
  }

  async function enableTx() {
    setBusy(true)
    try {
      await ensureSessionReady()
      setLog('Bench TX enabled (analysis mode — no full synthetic peers)')
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
      await ensureSessionReady()
      const res = await api.hostDrive({
        speed_mmps: speed,
        yaw_rate_mrad_s: yaw,
        gear,
        period_ms: periodic ? periodMs : null,
      })
      setLog(`host-drive: ${JSON.stringify(res)}`)
      await refresh()
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
      setLog('Stop All / analysis stopped')
      await refresh()
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="workspace" data-testid="workspace-control">
      <h1>Control</h1>
      <p className="muted">
        Safety-bypass style: do not fake the whole network. Inject only the host drive
        command under analysis (yaw rate, speed, gear) on the virtual high bus.
      </p>

      <div className="form-grid">
        <label>
          Speed (mm/s)
          <input
            data-testid="input-speed"
            type="number"
            value={speed}
            onChange={(e) => setSpeed(Number(e.target.value))}
          />
        </label>
        <label>
          Yaw rate (mrad/s)
          <input
            data-testid="input-yaw"
            type="number"
            value={yaw}
            onChange={(e) => setYaw(Number(e.target.value))}
          />
        </label>
        <label>
          Gear (0–3)
          <input
            data-testid="input-gear"
            type="number"
            min={0}
            max={3}
            value={gear}
            onChange={(e) => setGear(Number(e.target.value))}
          />
        </label>
        <label>
          Period (ms)
          <input
            data-testid="input-period"
            type="number"
            value={periodMs}
            disabled={!periodic}
            onChange={(e) => setPeriodMs(Number(e.target.value))}
          />
        </label>
        <label className="check">
          <input
            data-testid="check-periodic"
            type="checkbox"
            checked={periodic}
            onChange={(e) => setPeriodic(e.target.checked)}
          />
          Periodic (re-encode each period)
        </label>
      </div>

      <div className="actions">
        <button type="button" data-testid="btn-enable-tx" disabled={busy} onClick={() => void enableTx()}>
          Enable Bench TX
        </button>
        <button
          type="button"
          data-testid="btn-inject-drive"
          disabled={busy}
          onClick={() => void injectHostDrive()}
        >
          Inject host drive
        </button>
        <button
          type="button"
          data-testid="btn-stop-all"
          disabled={busy}
          className="danger"
          onClick={() => void stopAll()}
        >
          Stop All
        </button>
      </div>
      <pre className="log" data-testid="control-log">
        {log || 'Ready.'}
      </pre>
    </div>
  )
}

export default function App() {
  useBackendStream()
  const workspace = useAppStore((s) => s.workspace)
  return (
    <div className="app" data-testid="app">
      <Topbar />
      <div className="body">
        <Sidebar />
        <main>
          {workspace === 'overview' && <Overview />}
          {workspace === 'live' && <LiveCan />}
          {workspace === 'control' && <Control />}
        </main>
      </div>
    </div>
  )
}
