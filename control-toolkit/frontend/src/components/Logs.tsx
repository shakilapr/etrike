import { useCallback, useEffect, useState } from 'react'
import { api } from '../api'
import { Button } from './ui/button'
import { Card } from './ui/card'
import { Input } from './ui/input'
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
  const [q, setQ] = useState('')
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
        q: q.trim() || undefined,
      })
      setLogs(Array.isArray(r.logs) ? r.logs : [])
      setStats(r.stats && typeof r.stats === 'object' ? r.stats : null)
      setErr('')
    } catch (e) {
      setLogs([])
      setErr(String(e))
    }
  }, [category, severity, q])

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

  return (
    <WorkspaceShell
      testId="workspace-logs"
      title="Logging"
      description="Operational audit trail (architecture §7 / §14). Session, transport, control, and safety events."
    >

      <Card>
        <div className="toolbar logs-toolbar flex min-w-0 flex-wrap items-center gap-2.5">
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
            placeholder="Search code, title, detail…"
            value={q}
            onChange={(e) => setQ(e.target.value)}
          />
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
            {String(stats.count ?? 0)} / {String(stats.capacity ?? '—')} entries · seq{' '}
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
                  <th>Age</th>
                  <th>Sev</th>
                  <th>Cat</th>
                  <th>Code</th>
                  <th>Title</th>
                  <th>Detail</th>
                </tr>
              </thead>
              <tbody>
                {logs.map((e) => (
                  <tr
                    key={String(e.log_id)}
                    className={
                      selected?.log_id === e.log_id ? 'selected' : undefined
                    }
                    data-testid={`log-row-${String(e.log_id)}`}
                    onClick={() => setSelected(e)}
                  >
                    <td className="mono num">
                      {typeof e.age_s === 'number'
                        ? `${(e.age_s as number).toFixed(1)}s`
                        : '—'}
                    </td>
                    <td>
                      <span className={`log-sev log-sev-${String(e.severity)}`}>
                        {String(e.severity)}
                      </span>
                    </td>
                    <td className="mono">{String(e.category)}</td>
                    <td className="mono">{String(e.code)}</td>
                    <td>{String(e.title)}</td>
                    <td className="muted small">{String(e.detail || '')}</td>
                  </tr>
                ))}
                {logs.length === 0 && (
                  <tr>
                    <td colSpan={6} className="muted">
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
