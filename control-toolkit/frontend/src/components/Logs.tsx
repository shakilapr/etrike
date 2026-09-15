import { useCallback, useEffect, useState } from 'react'
import { api } from '../api'
import { Button } from './ui/button'
import { Card } from './ui/card'
import { Input } from './ui/input'
import { Seg, SegButton } from './ui/seg'
import { WorkspaceShell } from './WorkspaceShell'

const LOG_CATEGORIES = [
  'all',
  'system',
  'session',
  'transport',
  'control',
  'inject',
  'safety',
  'recording',
  'test',
  'protocol',
  'hmi',
  'api',
] as const

export function Logs() {
  const [logs, setLogs] = useState<Array<Record<string, unknown>>>([])
  const [stats, setStats] = useState<Record<string, unknown> | null>(null)
  const [category, setCategory] = useState<string>('all')
  const [severity, setSeverity] = useState<string>('all')
  const [bus, setBus] = useState<string>('all')
  const [q, setQ] = useState('')
  const [hideLiveness, setHideLiveness] = useState(true)
  const [err, setErr] = useState('')
  const [busy, setBusy] = useState(false)
  const [selected, setSelected] = useState<Record<string, unknown> | null>(null)
  const [auto, setAuto] = useState(true)

  const refresh = useCallback(async () => {
    try {
      const r = await api.logs({
        limit: 400,
        category: category === 'all' ? undefined : category,
        severity: severity === 'all' ? undefined : severity,
        bus: bus === 'all' ? undefined : bus,
        q: q.trim() || undefined,
      })
      setLogs(Array.isArray(r.logs) ? r.logs : [])
      setStats(r.stats && typeof r.stats === 'object' ? r.stats : null)
      setErr('')
    } catch (e) {
      setLogs([])
      setErr(String(e))
    }
  }, [category, severity, bus, q])

  useEffect(() => {
    void refresh()
    if (!auto) return
    const id = window.setInterval(() => void refresh(), 1500)
    return () => window.clearInterval(id)
  }, [refresh, auto])

  async function clearAll() {
    setBusy(true)
    try {
      await api.clearLogs()
      await refresh()
    } catch (e) {
      setErr(String(e))
    } finally {
      setBusy(false)
    }
  }

  function exportJson() {
    const blob = new Blob([JSON.stringify({ stats, logs }, null, 2)], {
      type: 'application/json',
    })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = `control-toolkit-logs-${Date.now()}.json`
    a.click()
    URL.revokeObjectURL(url)
  }

  const filteredLogs = logs.filter((e) => {
    if (hideLiveness && String(e.code) === 'protocol.node_liveness') {
      return false
    }
    return true
  })

  function formatWallTime(ts?: unknown): string {
    if (typeof ts !== 'number' || !ts) return '—'
    const d = new Date(ts * 1000)
    const hh = String(d.getHours()).padStart(2, '0')
    const mm = String(d.getMinutes()).padStart(2, '0')
    const ss = String(d.getSeconds()).padStart(2, '0')
    const ms = String(d.getMilliseconds()).padStart(3, '0')
    return `${hh}:${mm}:${ss}.${ms}`
  }

  return (
    <WorkspaceShell
      testId="workspace-logs"
      title="Logging"
      description="Operational audit trail. Session, transport, control, and safety events."
      sectionLabel="Analysis"
    >
      <Card>
        {/* Preset quick filter chips */}
        <div className="logs-presets mb-2.5">
          <button
            type="button"
            className={`logs-preset-btn ${category === 'all' && severity === 'all' ? 'active' : ''}`}
            onClick={() => {
              setCategory('all')
              setSeverity('all')
              setBus('all')
            }}
          >
            All Logs
          </button>
          <button
            type="button"
            className={`logs-preset-btn ${category === 'safety' ? 'active' : ''}`}
            onClick={() => {
              setCategory('safety')
              setSeverity('all')
            }}
          >
            ⚠ Safety & ESTOP
          </button>
          <button
            type="button"
            className={`logs-preset-btn ${severity === 'warning' ? 'active' : ''}`}
            onClick={() => {
              setCategory('all')
              setSeverity('warning')
            }}
          >
            Warnings & Faults
          </button>
          <button
            type="button"
            className={`logs-preset-btn ${category === 'control' || category === 'inject' ? 'active' : ''}`}
            onClick={() => {
              setCategory('control')
              setSeverity('all')
            }}
          >
            ⚡ Control & Inject
          </button>
        </div>

        <div className="toolbar logs-toolbar flex min-w-0 flex-wrap items-center gap-2.5">
          <Seg data-testid="logs-bus-seg">
            {(['all', 'high', 'low'] as const).map((b) => (
              <SegButton
                key={b}
                active={bus === b}
                onClick={() => setBus(b)}
                data-testid={`logs-bus-${b}`}
              >
                {b === 'all' ? 'All Buses' : b.toUpperCase()}
              </SegButton>
            ))}
          </Seg>
          <select
            data-testid="logs-category"
            value={category}
            onChange={(e) => setCategory(e.target.value)}
            aria-label="Log category"
          >
            {LOG_CATEGORIES.map((c) => (
              <option key={c} value={c}>
                {c === 'all' ? 'All categories' : c}
              </option>
            ))}
          </select>
          <select
            data-testid="logs-severity"
            value={severity}
            onChange={(e) => setSeverity(e.target.value)}
            aria-label="Log severity"
          >
            {['all', 'debug', 'info', 'warning', 'error', 'critical'].map((s) => (
              <option key={s} value={s}>
                {s === 'all' ? 'All severities' : s}
              </option>
            ))}
          </select>
          <Input
            search
            data-testid="logs-filter"
            placeholder="Search code, title, bus, CAN ID (0x300), detail…"
            value={q}
            onChange={(e) => setQ(e.target.value)}
          />
          <label className="check" title="Suppress repeated periodic node liveness frames">
            <input
              type="checkbox"
              data-testid="logs-hide-liveness"
              checked={hideLiveness}
              onChange={(e) => setHideLiveness(e.target.checked)}
            />
            Hide liveness pings
          </label>
          <label className="check">
            <input
              type="checkbox"
              data-testid="logs-auto"
              checked={auto}
              onChange={(e) => setAuto(e.target.checked)}
            />
            Auto-refresh
          </label>
          <Button
            variant="secondary"
            data-testid="logs-refresh"
            disabled={busy}
            onClick={() => void refresh()}
          >
            Refresh
          </Button>
          <Button variant="secondary" data-testid="logs-export" onClick={() => exportJson()}>
            Export JSON
          </Button>
          <Button
            variant="danger"
            data-testid="logs-clear"
            disabled={busy}
            onClick={() => void clearAll()}
          >
            Clear
          </Button>
        </div>
        {stats && (
          <p className="muted small" data-testid="logs-stats">
            {String(filteredLogs.length)} shown ({String(stats.count ?? 0)} total) / {String(stats.capacity ?? '—')} capacity · seq{' '}
            {String(stats.sequence ?? '—')}
          </p>
        )}
        {err && <p className="danger-text">{err}</p>}
      </Card>

      <div className="logs-split">
        <section className="panel" data-testid="logs-table-panel">
          <div className="table-wrap logs-table-wrap">
            <table className="can-table" data-testid="logs-table">
              <thead>
                <tr>
                  <th>Timestamp</th>
                  <th>Sev</th>
                  <th>Cat</th>
                  <th>Bus / ID</th>
                  <th>Code</th>
                  <th>Title</th>
                  <th>Detail</th>
                </tr>
              </thead>
              <tbody>
                {filteredLogs.map((e) => {
                  const repeatCount = Number(e.repeat_count ?? 1)
                  return (
                    <tr
                      key={String(e.log_id)}
                      className={
                        selected?.log_id === e.log_id ? 'selected' : undefined
                      }
                      data-testid={`log-row-${String(e.log_id)}`}
                      onClick={() => setSelected(e)}
                    >
                      <td>
                        <div className="log-time-cell">
                          <span className="log-time-wall">{formatWallTime(e.last_wall || e.ts_wall)}</span>
                          <span className="log-time-age">
                            {typeof e.age_s === 'number'
                              ? `${(e.age_s as number).toFixed(1)}s ago`
                              : '—'}
                          </span>
                        </div>
                      </td>
                      <td>
                        <span className={`log-sev log-sev-${String(e.severity)}`}>
                          {String(e.severity)}
                        </span>
                      </td>
                      <td className="mono">{String(e.category)}</td>
                      <td className="mono small">
                        {e.bus ? (
                          <span className="badge badge-subtle">
                            {String(e.bus)}
                            {e.can_id != null
                              ? ` 0x${Number(e.can_id).toString(16).toUpperCase()}`
                              : ''}
                          </span>
                        ) : (
                          <span className="muted">—</span>
                        )}
                      </td>
                      <td className="mono">
                        {String(e.code)}
                        {repeatCount > 1 && (
                          <span className="log-repeat-badge" title={`Repeated ${repeatCount} times without changing`}>
                            {repeatCount}×
                          </span>
                        )}
                      </td>
                      <td>{String(e.title)}</td>
                      <td className="muted small">{String(e.detail || '')}</td>
                    </tr>
                  )
                })}
                {filteredLogs.length === 0 && (
                  <tr>
                    <td colSpan={7} className="muted">
                      No log entries match filters.
                    </td>
                  </tr>
                )}
              </tbody>
            </table>
          </div>
        </section>

        <aside className="panel" data-testid="logs-detail">
          <h2>Entry detail</h2>
          {!selected && (
            <p className="muted small">Select a row to inspect full payload.</p>
          )}
          {selected && (
            <dl className="kv">
              <dt>ID</dt>
              <dd className="mono">{String(selected.log_id)}</dd>
              <dt>Code</dt>
              <dd className="mono">{String(selected.code)}</dd>
              <dt>Category</dt>
              <dd>{String(selected.category)}</dd>
              <dt>Severity</dt>
              <dd>{String(selected.severity)}</dd>
              <dt>Title</dt>
              <dd>{String(selected.title)}</dd>
              <dt>Detail</dt>
              <dd>{String(selected.detail || '—')}</dd>
              <dt>Bus / ID</dt>
              <dd className="mono">
                {String(selected.bus ?? '—')} ·{' '}
                {selected.can_id != null
                  ? `0x${Number(selected.can_id).toString(16).toUpperCase()}`
                  : '—'}
              </dd>
              <dt>Session</dt>
              <dd className="mono">{String(selected.session_id ?? '—')}</dd>
              <dt>Data</dt>
              <dd>
                <pre className="log" data-testid="logs-detail-data">
                  {JSON.stringify(selected.data ?? {}, null, 2)}
                </pre>
              </dd>
            </dl>
          )}
        </aside>
      </div>
    </WorkspaceShell>
  )
}
