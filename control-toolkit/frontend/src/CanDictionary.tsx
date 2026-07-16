/**
 * CAN Dictionary — compact expandable message table.
 * Data from YAML-generated protocol catalog via API.
 * (Avoids MessageCard stacks that re-list the same signals 3–4 ways.)
 */
import { useCallback, useEffect, useMemo, useState } from 'react'
import { api } from './api'

const FIELD_COLORS = [
  '#1f6feb',
  '#cf222e',
  '#1a7f37',
  '#9a6700',
  '#8250df',
  '#bf3989',
  '#0969da',
  '#bc4c00',
  '#116329',
  '#a40e26',
  '#6639ba',
  '#0550ae',
]

export type DictField = {
  key: string
  label: string
  kind: string
  unit?: string
  min?: number | null
  max?: number | null
  options?: Array<{ value: number | string; label: string }> | null
  _byte: number
  _bit_offset: number
  _size: number
  _type: string
  _factor: number
  _offset: number
}

export type DictMessage = {
  bus: string
  id: string
  can_id: number
  name: string
  sender: string
  dlc: number
  period: string
  receivers: string[]
  comment?: string
  byteOrder: string
  fields: DictField[]
  canonicalKey: string
  layout_kind?: string
  source?: string
  capabilities?: Record<string, unknown>
}

type BusFilter = 'all' | 'high' | 'low'

function colorFor(index: number): string {
  return FIELD_COLORS[index % FIELD_COLORS.length]
}

function scaleFor(signal: DictField): string {
  const f = signal._factor
  const o = signal._offset
  if (f === 1 && o === 0) return 'raw'
  if (f === 1) return `raw ${o >= 0 ? '+' : '−'} ${Math.abs(o)}`
  if (o === 0) return `raw × ${f}`
  return `raw × ${f} ${o >= 0 ? '+' : '−'} ${Math.abs(o)}`
}

function valuesFor(signal: DictField): string {
  if (!signal.options?.length) return '—'
  return signal.options.map((opt) => `${opt.value}=${opt.label}`).join(', ')
}

function dash(value: string | number | null | undefined): string {
  if (value === undefined || value === null || value === '') return '—'
  return String(value)
}

function unitFor(signal: DictField): string {
  if (signal.unit) return signal.unit
  if (/_mmps$/i.test(signal.key)) return 'mm/s'
  if (/_mrad_s$/i.test(signal.key)) return 'mrad/s'
  if (/_kpa$/i.test(signal.key)) return 'kPa'
  return ''
}

function titleFor(signal: DictField): string {
  let k = signal.key
    .replace(/_mmps$/i, '')
    .replace(/_mrad_s$/i, '')
    .replace(/_raw$/i, '')
  const words = k.split(/_+/).filter(Boolean)
  if (!words.length) return signal.label || signal.key
  return words
    .map((w) => {
      const lower = w.toLowerCase()
      if (lower === 'estop') return 'E-stop'
      return lower.charAt(0).toUpperCase() + lower.slice(1)
    })
    .join(' ')
}

function meaningFor(signal: DictField): string {
  const KNOWN: Record<string, string> = {
    speed_mmps: 'Host longitudinal speed command (mm/s).',
    yaw_rate_mrad_s: 'Host yaw-rate command (mrad/s).',
    gear: 'Requested gear / drive mode.',
    gear_state: 'Reported gear from motor controller.',
    motor_speed_mmps: 'Low-bus direct motor speed command (mm/s).',
    actual_speed_mmps: 'Measured motor speed feedback (mm/s).',
    estop_active: 'Emergency-stop latched (1 = active).',
    heartbeat_ok: 'SYS safety heartbeat healthy.',
    light_left: 'Left turn / indicator lamp.',
    light_right: 'Right turn / indicator lamp.',
    light_brake: 'Brake lamp.',
    light_head: 'Headlamp.',
    fault_flags: 'Motor fault / status bitfield.',
    target_angle_raw: 'Steering target angle (vendor raw).',
    pressure_request_raw: 'Brake pressure request (vendor raw).',
    rolling_counter: 'Rolling counter for freshness.',
    checksum: 'Payload checksum.',
    req_mode: 'HMI mode request (manual/auto).',
    req_start: 'HMI power start/stop request.',
  }
  if (KNOWN[signal.key]) return KNOWN[signal.key]
  if (signal.kind === 'boolean' || signal._size === 1) {
    return `Flag: ${titleFor(signal)} (0/1).`
  }
  if (signal.kind === 'enum' && signal.options?.length) {
    return `Enum: ${valuesFor(signal)}`
  }
  const u = unitFor(signal)
  return u ? `${titleFor(signal)} (${u}).` : `${titleFor(signal)}.`
}

function msgKey(m: DictMessage): string {
  return `${m.bus}:${m.id}:${m.name}`
}

function ExpandedSignals({ message }: { message: DictMessage }) {
  if (message.fields.length === 0) {
    return (
      <div className="signal-empty" data-testid="dict-signal-table">
        {message.dlc === 0
          ? 'DLC=0 event frame — no payload signals (CAN ID is the event).'
          : 'No payload signals (opaque / event layout).'}
      </div>
    )
  }

  return (
    <div className="dict-expand-body" data-testid="dictionary-detail">
      {message.comment ? <p className="message-comment">{message.comment}</p> : null}
      <div className="signal-table-wrap" data-testid="dict-signal-table">
        <table className="signal-table dict-signal-table">
          <thead>
            <tr>
              <th>Signal</th>
              <th>Meaning</th>
              <th>Start</th>
              <th>Len</th>
              <th>Type</th>
              <th>Scale</th>
              <th>Min</th>
              <th>Max</th>
              <th>Unit</th>
              <th>Values</th>
            </tr>
          </thead>
          <tbody>
            {message.fields.map((signal, index) => (
              <tr key={signal.key}>
                <td data-label="Signal">
                  <span
                    className="sig-color"
                    style={{ background: colorFor(index) }}
                    aria-hidden
                  />
                  <div className="sig-name-stack">
                    <strong>{titleFor(signal)}</strong>
                    <span className="mono sig-key">{signal.key}</span>
                  </div>
                </td>
                <td data-label="Meaning" className="sig-meaning-cell">
                  {meaningFor(signal)}
                </td>
                <td data-label="Start" className="mono">
                  B{signal._byte}.{signal._bit_offset}
                </td>
                <td data-label="Len" className="mono">
                  {signal._size}
                </td>
                <td data-label="Type">
                  {signal._type}
                  {signal._size === 1
                    ? ' flag'
                    : signal.kind === 'enum'
                      ? ' enum'
                      : ''}
                </td>
                <td data-label="Scale" className="mono small">
                  {scaleFor(signal)}
                </td>
                <td data-label="Min" className="mono">
                  {dash(signal.min)}
                </td>
                <td data-label="Max" className="mono">
                  {dash(signal.max)}
                </td>
                <td data-label="Unit">{dash(unitFor(signal))}</td>
                <td data-label="Values">{valuesFor(signal)}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  )
}

export function CanDictionary() {
  const [messages, setMessages] = useState<DictMessage[]>([])
  const [hash, setHash] = useState('')
  const [source, setSource] = useState('')
  const [busFilter, setBusFilter] = useState<BusFilter>('all')
  const [filterText, setFilterText] = useState('')
  const [err, setErr] = useState('')
  const [busy, setBusy] = useState(false)
  const [loadedAt, setLoadedAt] = useState('')
  /** Single expanded message key (or null). */
  const [expanded, setExpanded] = useState<string | null>(null)

  const load = useCallback(async (refresh = false) => {
    setBusy(true)
    setErr('')
    try {
      const r = refresh
        ? await api.refreshDictionary()
        : await api.protocolDictionary()
      setMessages((r.messages || []) as DictMessage[])
      setHash(r.semantic_hash || r.wire_hash || '')
      setSource(r.source || 'YAML')
      setLoadedAt(new Date().toLocaleTimeString())
    } catch (e) {
      setErr(String(e))
    } finally {
      setBusy(false)
    }
  }, [])

  useEffect(() => {
    void load(false)
  }, [load])

  const filtered = useMemo(() => {
    const text = filterText.trim().toLowerCase()
    return messages.filter((message) => {
      const matchesBus = busFilter === 'all' || message.bus === busFilter
      if (!matchesBus) return false
      if (!text) return true
      return (
        message.id.toLowerCase().includes(text) ||
        message.name.toLowerCase().includes(text) ||
        message.sender.toLowerCase().includes(text) ||
        (message.comment || '').toLowerCase().includes(text) ||
        message.receivers.some((r) => r.toLowerCase().includes(text)) ||
        message.fields.some(
          (s) =>
            s.label.toLowerCase().includes(text) ||
            s.key.toLowerCase().includes(text),
        )
      )
    })
  }, [messages, busFilter, filterText])

  const visibleSignals = useMemo(
    () => filtered.reduce((n, m) => n + m.fields.length, 0),
    [filtered],
  )

  function toggle(m: DictMessage) {
    const k = msgKey(m)
    setExpanded((cur) => (cur === k ? null : k))
  }

  return (
    <div className="workspace dict-workspace" data-testid="workspace-dictionary">
      <div className="panel monitor-panel dict-panel">
        <div className="toolbar dict-toolbar">
          <div className="toolbar-main">
            <div className="dictionary-title">
              <h1>CAN Dictionary</h1>
              <span>Signal reference · YAML protocol</span>
            </div>
            <div className="bus-tabs" role="tablist" aria-label="Bus filter">
              {(['all', 'high', 'low'] as const).map((b) => (
                <button
                  key={b}
                  type="button"
                  role="tab"
                  data-testid={`dict-bus-${b}`}
                  className={busFilter === b ? 'active' : undefined}
                  aria-selected={busFilter === b}
                  onClick={() => setBusFilter(b)}
                >
                  {b === 'all' ? 'All' : b === 'high' ? 'High' : 'Low'}
                </button>
              ))}
            </div>
            <input
              className="dictionary-search search"
              data-testid="dict-filter"
              placeholder="Search by CAN ID, name, signal, ECU…"
              value={filterText}
              onChange={(e) => setFilterText(e.target.value)}
            />
            <button
              type="button"
              className="secondary"
              data-testid="dict-refresh"
              disabled={busy}
              title="Reload dictionary from YAML-generated protocol catalog"
              onClick={() => void load(true)}
            >
              {busy ? 'Refreshing…' : 'Refresh'}
            </button>
          </div>
          <div className="dictionary-count">
            <span>{filtered.length} messages</span>
            <span>{visibleSignals} signals</span>
          </div>
        </div>

        <div className="dictionary-summary">
          <span>{messages.length} canonical protocol messages</span>
          <span className="mono" title={hash}>
            hash {(hash || '—').slice(0, 12)}…
          </span>
          <span>{source || 'YAML'}</span>
          {loadedAt ? <span>loaded {loadedAt}</span> : null}
          <span className="muted">Click a row to expand signals</span>
        </div>

        {err && <p className="danger-text dict-err">{err}</p>}

        <div className="dictionary-reference" data-testid="dict-grid">
          {filtered.length === 0 && !busy ? (
            <div className="empty-state">No CAN dictionary messages match the current filters.</div>
          ) : (
            <div className="dict-table-wrap">
              <table className="dict-msg-table" data-testid="dict-msg-table">
                <thead>
                  <tr>
                    <th className="dict-col-exp" aria-label="Expand" />
                    <th>ID</th>
                    <th>Name</th>
                    <th>Bus</th>
                    <th>TX</th>
                    <th>RX</th>
                    <th>DLC</th>
                    <th>Period</th>
                    <th>Order</th>
                    <th>Signals</th>
                  </tr>
                </thead>
                <tbody>
                  {filtered.map((message) => {
                    const k = msgKey(message)
                    const open = expanded === k
                    const rx =
                      message.receivers?.length > 0
                        ? message.receivers.join(', ')
                        : '—'
                    return (
                      <MessageRows
                        key={k}
                        message={message}
                        open={open}
                        rx={rx}
                        onToggle={() => toggle(message)}
                      />
                    )
                  })}
                </tbody>
              </table>
            </div>
          )}
        </div>
      </div>
    </div>
  )
}

function MessageRows({
  message,
  open,
  rx,
  onToggle,
}: {
  message: DictMessage
  open: boolean
  rx: string
  onToggle: () => void
}) {
  return (
    <>
      <tr
        className={`dict-msg-row${open ? ' open' : ''}`}
        data-testid="frame-row"
        data-msg={`${message.bus}:${message.id}`}
        onClick={onToggle}
        aria-expanded={open}
      >
        <td className="dict-col-exp mono" aria-hidden>
          {open ? '▾' : '▸'}
        </td>
        <td className="mono message-id">{message.id}</td>
        <td>
          <strong>{message.name}</strong>
        </td>
        <td>
          <span className={`dict-bus-pill ${message.bus}`}>{message.bus}</span>
        </td>
        <td className="mono">{message.sender}</td>
        <td className="mono muted">{rx}</td>
        <td className="num mono">{message.dlc}</td>
        <td className="mono">{message.period}</td>
        <td className="muted">{message.byteOrder}</td>
        <td className="num mono">{message.fields.length}</td>
      </tr>
      {open ? (
        <tr className="dict-msg-expand-row" data-testid={`dict-expand-${message.id}`}>
          <td colSpan={10}>
            <ExpandedSignals message={message} />
          </td>
        </tr>
      ) : null}
    </>
  )
}
