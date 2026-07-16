/**
 * CAN Dictionary workspace — structure ported from debug-tool
 * (CanDictionary / MessageCard / BitGrid / SignalTable).
 * Data is always loaded from YAML-generated protocol catalog via API.
 */
import {
  useCallback,
  useEffect,
  useMemo,
  useState,
  type CSSProperties,
} from 'react'
import { api } from './api'

/** High-contrast palette — consecutive signals stay visually distinct (no pale blues). */
const FIELD_COLORS = [
  '#1f6feb', // blue
  '#cf222e', // red
  '#1a7f37', // green
  '#9a6700', // amber
  '#8250df', // purple
  '#bf3989', // magenta
  '#0969da', // sky
  '#bc4c00', // orange
  '#116329', // forest
  '#a40e26', // crimson
  '#6639ba', // violet
  '#0550ae', // navy
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
  if (f === 1) return `raw ${o >= 0 ? '+' : '-'} ${Math.abs(o)}`
  if (o === 0) return `raw x ${f}`
  return `raw x ${f} ${o >= 0 ? '+' : '-'} ${Math.abs(o)}`
}

function valuesFor(signal: DictField): string {
  if (!signal.options?.length) return '—'
  return signal.options.map((opt) => `${opt.value}=${opt.label}`).join(', ')
}

function dash(value: string | number | null | undefined): string {
  if (value === undefined || value === null || value === '') return '—'
  return String(value)
}

function signalGlyph(label: string): string {
  const t = label.trim()
  if (!t) return '·'
  // Prefer leading letter/digit; fall back to first char
  const m = t.match(/[A-Za-z0-9]/)
  return (m?.[0] ?? t[0]).toUpperCase()
}

function BitGrid({
  message,
  activeSignal,
  setActiveSignal,
}: {
  message: DictMessage
  activeSignal: number
  setActiveSignal: (i: number) => void
}) {
  const dlc = Math.max(message.dlc, 0)
  const bitMap = useMemo(() => {
    const map = Array.from({ length: dlc * 8 }, () => -1)
    message.fields.forEach((signal, index) => {
      const start = signal._byte * 8 + signal._bit_offset
      for (let offset = 0; offset < signal._size; offset++) {
        const bit = start + offset
        if (bit >= 0 && bit < map.length) map[bit] = index
      }
    })
    return map
  }, [message.fields, dlc])

  /** Linear bit index of the first bit of each signal (for in-cell glyph). */
  const fieldStartBit = useMemo(() => {
    const starts = new Map<number, number>()
    message.fields.forEach((signal, index) => {
      starts.set(index, signal._byte * 8 + signal._bit_offset)
    })
    return starts
  }, [message.fields])

  const mappedBits = bitMap.filter((i) => i >= 0).length
  const unusedBits = dlc * 8 - mappedBits
  const wide = dlc >= 7
  const dimOthers = activeSignal >= 0

  if (dlc === 0) {
    return (
      <div className="bit-empty">DLC=0 event frame. The CAN ID is the signal.</div>
    )
  }

  const active = activeSignal >= 0 ? message.fields[activeSignal] : null
  const activeColor = activeSignal >= 0 ? colorFor(activeSignal) : undefined

  return (
    <div
      className={`dict-bitgrid${dimOthers ? ' has-focus' : ''}`}
      data-testid="dict-bit-grid"
    >
      <div className="bit-grid-head">
        <span>Byte layout</span>
        <em>
          {mappedBits}/{dlc * 8} bits mapped
          {unusedBits > 0 ? ` · ${unusedBits} unused` : ''}
        </em>
      </div>

      {message.fields.length > 0 || unusedBits > 0 ? (
        <div className="bit-legend" data-testid="dict-bit-legend" aria-label="Signal color key">
          {message.fields.map((signal, index) => (
            <button
              key={signal.key}
              type="button"
              className={`bit-legend-item${activeSignal === index ? ' active' : ''}`}
              style={
                {
                  ['--bit-color' as string]: colorFor(index),
                } as CSSProperties
              }
              title={`${signal.label} · B${signal._byte}.${signal._bit_offset} · ${signal._size}-bit`}
              onMouseEnter={() => setActiveSignal(index)}
              onMouseLeave={() => setActiveSignal(-1)}
              onFocus={() => setActiveSignal(index)}
              onBlur={() => setActiveSignal(-1)}
            >
              <span className="bit-legend-swatch" aria-hidden />
              <span className="bit-legend-glyph" aria-hidden>
                {signalGlyph(signal.label)}
              </span>
              <span className="bit-legend-label">{signal.label}</span>
              <span className="bit-legend-meta mono">
                B{signal._byte}.{signal._bit_offset}/{signal._size}
              </span>
            </button>
          ))}
          {unusedBits > 0 ? (
            <span className="bit-legend-item unused" title="Unmapped / reserved bits">
              <span className="bit-legend-swatch unused-swatch" aria-hidden />
              <span className="bit-legend-label">unused</span>
              <span className="bit-legend-meta mono">{unusedBits} bit{unusedBits === 1 ? '' : 's'}</span>
            </span>
          ) : null}
        </div>
      ) : null}

      <div className="byte-grid-scroll" aria-label={`${message.name} byte layout`}>
        <div className="byte-grid">
          {Array.from({ length: dlc }, (_, byte) => (
            <div key={byte} className="byte-col">
              <span className="byte-label">B{byte}</span>
              <div className="bit-row" role="group" aria-label={`Byte ${byte} bits 7→0`}>
                {[7, 6, 5, 4, 3, 2, 1, 0].map((bit) => {
                  const linear = byte * 8 + bit
                  const signalIndex = bitMap[linear] ?? -1
                  const filled = signalIndex >= 0
                  const highlight = activeSignal === signalIndex && filled
                  const dimmed =
                    dimOthers && filled && activeSignal !== signalIndex
                  const isStart =
                    filled && fieldStartBit.get(signalIndex) === linear
                  const glyph = filled
                    ? isStart
                      ? signalGlyph(message.fields[signalIndex].label)
                      : ''
                    : String(bit)
                  return (
                    <button
                      key={`${byte}-${bit}`}
                      type="button"
                      className={[
                        'bit-cell',
                        wide ? 'wide' : '',
                        filled ? 'filled' : 'empty',
                        highlight ? 'highlight' : '',
                        dimmed ? 'dimmed' : '',
                        isStart ? 'field-start' : '',
                      ]
                        .filter(Boolean)
                        .join(' ')}
                      style={
                        filled
                          ? ({
                              ['--bit-color' as string]: colorFor(signalIndex),
                            } as CSSProperties)
                          : undefined
                      }
                      title={
                        filled
                          ? `${message.fields[signalIndex].label}: B${byte}.${bit} · ${message.fields[signalIndex]._size}-bit ${message.fields[signalIndex]._type}`
                          : `B${byte}.${bit} unused (no signal)`
                      }
                      aria-label={
                        filled
                          ? `${message.fields[signalIndex].label} bit B${byte}.${bit}`
                          : `Unused bit B${byte}.${bit}`
                      }
                      onMouseEnter={() => setActiveSignal(signalIndex)}
                      onMouseLeave={() => setActiveSignal(-1)}
                      onFocus={() => setActiveSignal(signalIndex)}
                      onBlur={() => setActiveSignal(-1)}
                    >
                      <span>{glyph}</span>
                    </button>
                  )
                })}
              </div>
            </div>
          ))}
        </div>
      </div>
      <div
        className="bit-inspector"
        role="status"
        style={
          activeColor
            ? ({ ['--bit-color' as string]: activeColor } as CSSProperties)
            : undefined
        }
        data-active={active ? '1' : '0'}
      >
        {active ? (
          <>
            <span className="bit-inspector-swatch" aria-hidden />
            <strong>
              B{active._byte}.{active._bit_offset}
            </strong>
            <span>{active.label}</span>
            <em>
              {active._size}-bit {active._type} · scale {scaleFor(active)}
              {active.unit ? ` ${active.unit}` : ''}
            </em>
          </>
        ) : (
          <>
            <strong>Bit detail</strong>
            <span>{message.name}</span>
            <em>
              Hover a colored bit or legend chip — hatch / numbered cells are unused.
            </em>
          </>
        )}
      </div>
    </div>
  )
}

function SignalTable({
  message,
  activeSignal,
}: {
  message: DictMessage
  activeSignal: number
}) {
  if (message.fields.length === 0) {
    return <div className="signal-empty">No payload signals (opaque / event layout).</div>
  }

  const receivers =
    message.receivers?.length > 0 ? message.receivers.join(', ') : '—'

  return (
    <div className="signal-table-wrap" data-testid="dict-signal-table">
      <table className="signal-table">
        <thead>
          <tr>
            <th>Signal</th>
            <th>Start</th>
            <th>Byte</th>
            <th>Bit</th>
            <th>Len</th>
            <th>Type</th>
            <th>Scale</th>
            <th>Min</th>
            <th>Max</th>
            <th>Unit</th>
            <th>Rx</th>
            <th>Values</th>
            <th>Description</th>
          </tr>
        </thead>
        <tbody>
          {message.fields.map((signal, index) => (
            <tr
              key={signal.key}
              className={activeSignal === index ? 'highlight' : undefined}
            >
              <td data-label="Signal">
                <span
                  className="sig-color"
                  style={{ background: colorFor(index) }}
                />
                <strong>{signal.label}</strong>
              </td>
              <td data-label="Start">
                B{signal._byte}.{signal._bit_offset}
              </td>
              <td data-label="Byte">{signal._byte}</td>
              <td data-label="Bit">{signal._bit_offset}</td>
              <td data-label="Len">{signal._size}</td>
              <td data-label="Type">{signal._type}</td>
              <td data-label="Scale">{scaleFor(signal)}</td>
              <td data-label="Min">{dash(signal.min)}</td>
              <td data-label="Max">{dash(signal.max)}</td>
              <td data-label="Unit">{dash(signal.unit)}</td>
              <td data-label="Rx">{receivers}</td>
              <td data-label="Values">{valuesFor(signal)}</td>
              <td data-label="Description">{dash(signal.key)}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  )
}

function MessageCard({ message }: { message: DictMessage }) {
  const [activeSignal, setActiveSignal] = useState(-1)
  const receivers =
    message.receivers?.length > 0 ? message.receivers : (['all'] as string[])
  const signalCountLabel =
    message.fields.length === 1
      ? '1 signal'
      : `${message.fields.length} signals`

  return (
    <article
      className="message-card is-dictionary"
      data-testid="frame-row"
      data-msg={`${message.bus}:${message.id}`}
    >
      <div className="message-head">
        <span className="message-id">{message.id}</span>
        <strong>{message.name}</strong>
        <span className="message-bus">{message.bus}</span>
      </div>

      <div className="message-meta">
        <span className="badge sender" title="Sender ECU">
          TX {message.sender}
        </span>
        <span className="badge receiver" title="Receiver ECU(s)">
          RX {receivers.join(', ')}
        </span>
        <span className="badge" title="CAN payload length">
          DLC {message.dlc}
        </span>
        <span className="badge" title="Transmit period">
          {message.period}
        </span>
        <span className="badge" title="Signal byte order">
          {message.byteOrder}
        </span>
        <span className="badge" title="Signal count">
          {signalCountLabel}
        </span>
        <span className="badge source" title="Generated from protocol YAML">
          YAML
        </span>
      </div>

      <div
        className="route-map"
        aria-label={`${message.name} sender and receivers`}
      >
        <span className="route-node tx">TX {message.sender}</span>
        <span className="route-arrow" aria-hidden>
          -&gt;
        </span>
        <span className="route-receivers">
          {receivers.map((r) => (
            <span key={r} className="route-node rx">
              RX {r}
            </span>
          ))}
        </span>
      </div>

      <section className="dictionary-detail" data-testid="dictionary-detail">
        {message.comment ? (
          <p className="message-comment">{message.comment}</p>
        ) : null}
        <BitGrid
          message={message}
          activeSignal={activeSignal}
          setActiveSignal={setActiveSignal}
        />
        <SignalTable message={message} activeSignal={activeSignal} />
      </section>
    </article>
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
  const [loadedAt, setLoadedAt] = useState<string>('')

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
              placeholder="Search by CAN ID, name, signal, ECU, or comment"
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
            <span>
              {filtered.length} messages
            </span>
            <span>{visibleSignals} signals</span>
          </div>
        </div>

        <div className="dictionary-summary">
          <span>
            {messages.length} canonical protocol messages
          </span>
          <span className="mono" title={hash}>
            hash {(hash || '—').slice(0, 12)}…
          </span>
          <span>{source || 'YAML'}</span>
          {loadedAt ? <span>loaded {loadedAt}</span> : null}
        </div>

        {err && <p className="danger-text dict-err">{err}</p>}

        <div className="dictionary-reference" data-testid="dict-grid">
          {filtered.map((message) => (
            <MessageCard
              key={`${message.bus}:${message.id}:${message.name}`}
              message={message}
            />
          ))}
          {filtered.length === 0 && !busy && (
            <div className="empty-state">
              No CAN dictionary messages match the current filters.
            </div>
          )}
        </div>
      </div>
    </div>
  )
}
