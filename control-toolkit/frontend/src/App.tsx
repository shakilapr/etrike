import { useCallback, useState } from 'react'
import { useAppStore } from './store'
import { useBackendStream } from './useStream'
import { api } from './api'
import './App.css'

function FreshnessBadge({ value }: { value: string }) {
  return <span className={`fresh fresh-${value.toLowerCase()}`}>{value}</span>
}

function Topbar() {
  const status = useAppStore((s) => s.status)
  const quality = useAppStore((s) => s.streamQuality)
  const mismatch = useAppStore((s) => s.protocolMismatch)
  const high = status?.adapter?.channels?.high
  const low = status?.adapter?.channels?.low
  return (
    <header className="topbar">
      <div className="brand">Control Toolkit</div>
      <div className="chip">profile: {status?.session?.profile ?? status?.profile ?? '—'}</div>
      <div className="chip">adapter: {status?.adapter?.health ?? '—'}</div>
      <div className="chip">high: {high?.activity ?? '—'} ({high?.rx_count ?? 0})</div>
      <div className="chip">low: {low?.activity ?? '—'} ({low?.rx_count ?? 0})</div>
      <div className="chip">bench TX: {status?.session?.bench_tx ?? 'disabled'}</div>
      <div className={`chip quality-${quality}`}>stream: {quality.toUpperCase()}</div>
      {mismatch && <div className="chip danger">PROTOCOL MISMATCH</div>}
      <div className="chip mono muted">
        hash: {(status?.wire_hash ?? '').slice(0, 12)}…
      </div>
    </header>
  )
}

function Sidebar() {
  const workspace = useAppStore((s) => s.workspace)
  const setWorkspace = useAppStore((s) => s.setWorkspace)
  return (
    <nav className="sidebar">
      {(['overview', 'live', 'control'] as const).map((w) => (
        <button
          key={w}
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
  const byName = Object.fromEntries(messages.map((m) => [m.name, m]))
  const cards = [
    { label: 'SYS heartbeat', m: byName['SYS_HEARTBEAT'] },
    { label: 'Host heartbeat', m: byName['HOST_HEARTBEAT'] },
    { label: 'RT heartbeat (high)', m: messages.find((m) => m.name === 'RT_HEARTBEAT' && m.bus === 'high') },
    { label: 'RT heartbeat (low)', m: messages.find((m) => m.name === 'RT_HEARTBEAT' && m.bus === 'low') },
    { label: 'Host drive', m: byName['HOST_DRIVE_CMD'] },
  ]
  return (
    <div className="workspace">
      <h1>Overview</h1>
      <p className="muted">
        Pure Software monitor. Session {status?.session?.session_id ?? 'none'} · msgs{' '}
        {messages.length}
      </p>
      <div className="cards">
        {cards.map((c) => (
          <div className="card" key={c.label}>
            <div className="card-title">{c.label}</div>
            {c.m ? (
              <>
                <FreshnessBadge value={c.m.freshness} />
                <div className="mono">
                  {c.m.bus} 0x{c.m.can_id.toString(16).toUpperCase()} · {c.m.validation_status}
                </div>
                <ul className="signals">
                  {Object.entries(c.m.signals || {}).map(([k, v]) => {
                    const sig = v as {
                      engineering_value: number | string | null
                      unit?: string | null
                      enum_label?: string | null
                    }
                    return (
                      <li key={k}>
                        {k}: <strong>{String(sig.enum_label ?? sig.engineering_value)}</strong>
                        {sig.unit ? ` ${sig.unit}` : ''}
                      </li>
                    )
                  })}
                </ul>
              </>
            ) : (
              <div className="muted">No data</div>
            )}
          </div>
        ))}
      </div>
    </div>
  )
}

function LiveCan() {
  const messages = useAppStore((s) => s.messages)
  const sorted = [...messages].sort((a, b) => a.bus.localeCompare(b.bus) || a.can_id - b.can_id)
  return (
    <div className="workspace">
      <h1>Live CAN</h1>
      <table className="can-table">
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
            <tr key={`${m.bus}-${m.can_id}`}>
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
                No frames yet — enable Bench TX and start peers or inject.
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
  const [log, setLog] = useState<string>('')
  const [busy, setBusy] = useState(false)

  const refresh = useCallback(async () => {
    const st = await api.status()
    setStatus(st)
    return st
  }, [setStatus])

  async function ensureSession() {
    let st = await refresh()
    if (!st.session?.session_id) {
      await api.createSession('pure_software')
      st = await refresh()
    }
    return st
  }

  async function enableTx() {
    setBusy(true)
    try {
      const st = await ensureSession()
      const sid = st.session.session_id!
      await api.setBenchTx(sid, true, st.session.revision)
      setLog('Bench TX enabled')
      await refresh()
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function startPeers() {
    setBusy(true)
    try {
      await ensureSession()
      const st = await refresh()
      if (st.session.bench_tx !== 'enabled') {
        await api.setBenchTx(st.session.session_id!, true, st.session.revision)
      }
      const res = await api.startPeers(['host_heartbeat', 'sys_heartbeat', 'rt_heartbeat_high', 'rt_heartbeat_low'])
      setLog(`peers: ${JSON.stringify(res)}`)
      await refresh()
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function injectDrive() {
    setBusy(true)
    try {
      await ensureSession()
      const st = await refresh()
      if (st.session.bench_tx !== 'enabled') {
        await api.setBenchTx(st.session.session_id!, true, st.session.revision)
      }
      const res = await api.inject({
        bus: 'high',
        key: 'host:host_drive_cmd',
        values: { speed_mmps: 500, yaw_rate_mrad_s: 0, gear: 1 },
      })
      setLog(`inject: ${JSON.stringify(res)}`)
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
      if (!st.session?.session_id) return
      await api.stopAll(st.session.session_id, st.session.revision)
      setLog('Stop All done')
      await refresh()
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="workspace">
      <h1>Control</h1>
      <p className="muted">Virtual-bus stimuli only. Physical profiles are blocked until CANalyst lands.</p>
      <div className="actions">
        <button disabled={busy} onClick={() => void enableTx()}>
          Enable Bench TX
        </button>
        <button disabled={busy} onClick={() => void startPeers()}>
          Start synthetic peers
        </button>
        <button disabled={busy} onClick={() => void injectDrive()}>
          Inject HOST_DRIVE_CMD
        </button>
        <button disabled={busy} className="danger" onClick={() => void stopAll()}>
          Stop All
        </button>
      </div>
      <pre className="log">{log || 'Ready.'}</pre>
    </div>
  )
}

export default function App() {
  useBackendStream()
  const workspace = useAppStore((s) => s.workspace)
  return (
    <div className="app">
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
