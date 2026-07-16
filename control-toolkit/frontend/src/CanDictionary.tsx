/**
 * CAN Dictionary — message cards (debug-tool layout) with:
 *  - bit grid at top of each card
 *  - fixed-height hover inspector (no layout jump)
 *  - signal table rows expand on click to explain bit packing
 * Data from YAML-generated protocol catalog via API.
 */
import {
  Fragment,
  useCallback,
  useEffect,
  useMemo,
  useState,
  type CSSProperties,
} from 'react'
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
  if (f === 1 && o === 0) return 'raw = engineering'
  if (f === 1) return `engineering = raw ${o >= 0 ? '+' : '−'} ${Math.abs(o)}`
  if (o === 0) return `engineering = raw × ${f}`
  return `engineering = raw × ${f} ${o >= 0 ? '+' : '−'} ${Math.abs(o)}`
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

const MEANING: Record<string, string> = {
  speed_mmps: 'Host longitudinal speed command for RT kinematics (mm/s).',
  yaw_rate_mrad_s: 'Host yaw-rate command for RT kinematics (mrad/s).',
  gear: 'Requested gear / drive mode on the motion command.',
  gear_state: 'Gear currently reported by the motor controller.',
  motor_speed_mmps: 'Direct motor speed on Low bus (bypasses Host kinematics).',
  actual_speed_mmps: 'Measured motor speed feedback (mm/s).',
  estop_active: 'Vehicle emergency-stop latched (1 = ESTOP active).',
  heartbeat_ok: 'Safety heartbeat healthy flag from SYS.',
  light_left: 'Left turn / indicator lamp bit.',
  light_right: 'Right turn / indicator lamp bit.',
  light_brake: 'Brake lamp bit.',
  light_head: 'Headlamp bit.',
  fault_flags: 'Motor fault / status bitfield.',
  target_angle_raw: 'Steering target angle in vendor raw units.',
  pressure_request_raw: 'Brake pressure request in vendor raw units.',
  rolling_counter: 'Rolling counter for frame freshness.',
  checksum: 'Frame integrity checksum.',
  req_mode: 'HMI operating mode request.',
  req_start: 'HMI power start/stop request.',
}

function meaningFor(signal: DictField): string {
  if (MEANING[signal.key]) return MEANING[signal.key]
  if (signal.kind === 'boolean' || signal._size === 1) {
    return `Single flag bit: ${titleFor(signal)} (0 = off, 1 = on).`
  }
  if (signal.kind === 'enum' && signal.options?.length) {
    return `Enumerated “${titleFor(signal)}”: ${valuesFor(signal)}.`
  }
  const u = unitFor(signal)
  return u
    ? `Numeric “${titleFor(signal)}” in ${u}.`
    : `Protocol field “${titleFor(signal)}” (${signal._size}-bit ${signal._type}).`
}

function packingFor(signal: DictField): string {
  const end = signal._bit_offset + signal._size - 1
  const endByte = signal._byte + Math.floor(end / 8)
  const endBit = end % 8
  if (signal._size === 1) {
    return `Only B${signal._byte} bit ${signal._bit_offset}.`
  }
  return `B${signal._byte}.${signal._bit_offset} → B${endByte}.${endBit} (${signal._size} bits, ${signal._type}).`
}

function howBitsWork(signal: DictField, bitInField: number | null): string {
  const n = signal._size
  if (n === 1) {
    return `This flag occupies one wire bit. Value 0 = false/off, 1 = true/on (unless enum overrides).`
  }
  if (bitInField == null) {
    return (
      `Multi-bit field (${n} bits). All same-color cells in the grid belong to this signal. ` +
      `Bits pack contiguously from start B${signal._byte}.${signal._bit_offset}. ${scaleFor(signal)}.`
    )
  }
  if (bitInField === 0) {
    return `Bit 0/${n} — start of packing (LSB side of the field map). ${scaleFor(signal)}.`
  }
  if (bitInField === n - 1) {
    return `Bit ${bitInField}/${n} — end of packing (MSB side of the field map).`
  }
  return `Bit ${bitInField}/${n} of “${titleFor(signal)}” — middle of the multi-bit value.`
}

function signalGlyph(label: string): string {
  const t = label.trim()
  if (!t) return '·'
  const m = t.match(/[A-Za-z0-9]/)
  return (m?.[0] ?? t[0]).toUpperCase()
}

/* ── Bit grid (always shown) + fixed-height hover panel ───────────── */

function BitGrid({
  message,
  activeSignal,
  setActiveSignal,
  pinnedSignal,
}: {
  message: DictMessage
  activeSignal: number
  setActiveSignal: (i: number) => void
  pinnedSignal: number
}) {
  const dlc = Math.max(message.dlc, 0)
  const [hoverBit, setHoverBit] = useState(-1)

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

  const fieldStart = useMemo(() => {
    const m = new Map<number, number>()
    message.fields.forEach((s, i) => m.set(i, s._byte * 8 + s._bit_offset))
    return m
  }, [message.fields])

  if (dlc === 0) {
    return (
      <div className="bit-empty" data-testid="dict-bit-grid">
        DLC=0 event frame — no payload bits. The CAN ID itself is the signal.
      </div>
    )
  }

  const mapped = bitMap.filter((i) => i >= 0).length
  const showIdx = activeSignal >= 0 ? activeSignal : pinnedSignal
  const show = showIdx >= 0 ? message.fields[showIdx] : null
  const showColor = showIdx >= 0 ? colorFor(showIdx) : undefined
  const start = showIdx >= 0 ? (fieldStart.get(showIdx) ?? 0) : 0
  const bitInField =
    show && hoverBit >= 0 && activeSignal === showIdx ? hoverBit - start : null

  return (
    <div className="dict-bitgrid" data-testid="dict-bit-grid">
      <div className="bit-grid-head">
        <span>Byte layout</span>
        <em>
          {mapped}/{dlc * 8} bits mapped · hover a cell · bits 7→0 in each byte
        </em>
      </div>

      <div className="byte-grid-scroll" aria-label={`${message.name} byte layout`}>
        <div className="byte-grid">
          {Array.from({ length: dlc }, (_, byte) => (
            <div key={byte} className="byte-col">
              <span className="byte-label">B{byte}</span>
              <div className="bit-row" role="group" aria-label={`Byte ${byte} bits 7→0`}>
                {[7, 6, 5, 4, 3, 2, 1, 0].map((bit) => {
                  const linear = byte * 8 + bit
                  const si = bitMap[linear] ?? -1
                  const filled = si >= 0
                  const sig = filled ? message.fields[si] : null
                  const st = filled ? (fieldStart.get(si) ?? linear) : 0
                  const isStart = filled && st === linear
                  const isEnd = filled && sig != null && linear === st + sig._size - 1
                  const highlight =
                    filled && (activeSignal === si || pinnedSignal === si)
                  const dimmed =
                    (activeSignal >= 0 || pinnedSignal >= 0) &&
                    filled &&
                    activeSignal !== si &&
                    pinnedSignal !== si

                  let glyph = String(bit)
                  if (filled && sig) {
                    if (sig._size === 1) glyph = signalGlyph(sig.key)
                    else if (isStart) glyph = '›'
                    else if (isEnd) glyph = '‹'
                    else glyph = '·'
                  }

                  return (
                    <button
                      key={`${byte}-${bit}`}
                      type="button"
                      className={[
                        'bit-cell',
                        dlc >= 7 ? 'wide' : '',
                        filled ? 'filled' : 'empty',
                        highlight ? 'highlight' : '',
                        dimmed ? 'dimmed' : '',
                        isStart ? 'field-start' : '',
                        isEnd ? 'field-end' : '',
                      ]
                        .filter(Boolean)
                        .join(' ')}
                      style={
                        filled
                          ? ({
                              ['--bit-color' as string]: colorFor(si),
                            } as CSSProperties)
                          : undefined
                      }
                      title={
                        filled && sig
                          ? `${titleFor(sig)} (${sig.key}) · B${byte}.${bit}`
                          : `B${byte}.${bit} unused`
                      }
                      aria-label={
                        filled && sig
                          ? `${titleFor(sig)} at B${byte}.${bit}`
                          : `Unused B${byte}.${bit}`
                      }
                      onMouseEnter={() => {
                        setActiveSignal(si)
                        setHoverBit(linear)
                      }}
                      onMouseLeave={() => {
                        setActiveSignal(-1)
                        setHoverBit(-1)
                      }}
                      onFocus={() => {
                        setActiveSignal(si)
                        setHoverBit(linear)
                      }}
                      onBlur={() => {
                        setActiveSignal(-1)
                        setHoverBit(-1)
                      }}
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

      {/* Fixed height — hover content swaps in place; never grows the card */}
      <div
        className="bit-inspector bit-inspector-fixed"
        role="status"
        data-testid="dict-bit-inspector"
        data-active={show ? '1' : '0'}
        style={
          showColor
            ? ({ ['--bit-color' as string]: showColor } as CSSProperties)
            : undefined
        }
      >
        {show ? (
          <div className="bit-inspector-body">
            <div className="bit-inspector-title-row">
              <span className="bit-inspector-swatch" aria-hidden />
              <div>
                <strong className="bit-inspector-title">{titleFor(show)}</strong>
                <span className="bit-inspector-key mono">{show.key}</span>
              </div>
              {hoverBit >= 0 && activeSignal === showIdx ? (
                <span className="bit-inspector-cell mono">
                  B{Math.floor(hoverBit / 8)}.{hoverBit % 8}
                </span>
              ) : (
                <span className="bit-inspector-cell mono">
                  B{show._byte}.{show._bit_offset}
                </span>
              )}
            </div>
            <p className="bit-inspector-meaning">{meaningFor(show)}</p>
            <p className="bit-inspector-hint">{howBitsWork(show, bitInField)}</p>
          </div>
        ) : (
          <div className="bit-inspector-body idle">
            <strong>Bit hover</strong>
            <p>
              Hover a colored cell to see which signal owns it and how that bit fits the
              field. Hatched numbered cells are unused. Click a table row below for a
              full expand.
            </p>
          </div>
        )}
      </div>
    </div>
  )
}

/* ── Signal table with expand-on-click ────────────────────────────── */

function SignalTable({
  message,
  activeSignal,
  setActiveSignal,
  expandedSignal,
  setExpandedSignal,
}: {
  message: DictMessage
  activeSignal: number
  setActiveSignal: (i: number) => void
  expandedSignal: number
  setExpandedSignal: (i: number) => void
}) {
  if (message.fields.length === 0) {
    return (
      <div className="signal-empty" data-testid="dict-signal-table">
        No payload signals (opaque / event layout).
      </div>
    )
  }

  return (
    <div className="signal-table-wrap" data-testid="dict-signal-table">
      <table className="signal-table">
        <thead>
          <tr>
            <th className="dict-col-exp" aria-label="Expand" />
            <th>Signal</th>
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
          {message.fields.map((signal, index) => {
            const open = expandedSignal === index
            const hot = activeSignal === index || open
            return (
              <Fragment key={signal.key}>
                <tr
                  className={[hot ? 'highlight' : '', open ? 'row-open' : '']
                    .filter(Boolean)
                    .join(' ')}
                  data-testid={`dict-sig-row-${signal.key}`}
                  onMouseEnter={() => setActiveSignal(index)}
                  onMouseLeave={() => setActiveSignal(-1)}
                  onClick={() => setExpandedSignal(open ? -1 : index)}
                  style={{ cursor: 'pointer' }}
                >
                  <td className="dict-col-exp mono" aria-hidden>
                    {open ? '▾' : '▸'}
                  </td>
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
                  <td data-label="Start" className="mono">
                    B{signal._byte}.{signal._bit_offset}
                  </td>
                  <td data-label="Len">{signal._size}</td>
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
                  <td data-label="Min">{dash(signal.min)}</td>
                  <td data-label="Max">{dash(signal.max)}</td>
                  <td data-label="Unit">{dash(unitFor(signal) || signal.unit)}</td>
                  <td data-label="Values">{valuesFor(signal)}</td>
                </tr>
                {open ? (
                  <tr
                    className="dict-sig-expand"
                    data-testid={`dict-sig-expand-${signal.key}`}
                  >
                    <td colSpan={10}>
                      <div
                        className="dict-sig-expand-body"
                        style={
                          {
                            ['--bit-color' as string]: colorFor(index),
                          } as CSSProperties
                        }
                      >
                        <div className="dict-sig-expand-head">
                          <span className="bit-inspector-swatch" aria-hidden />
                          <strong>{titleFor(signal)}</strong>
                          <span className="mono sig-key">{signal.key}</span>
                        </div>
                        <p className="bit-inspector-meaning">{meaningFor(signal)}</p>
                        <dl className="bit-inspector-kv">
                          <div>
                            <dt>What the bits do</dt>
                            <dd>{howBitsWork(signal, null)}</dd>
                          </div>
                          <div>
                            <dt>Packing</dt>
                            <dd>{packingFor(signal)}</dd>
                          </div>
                          <div>
                            <dt>Scale</dt>
                            <dd className="mono">{scaleFor(signal)}</dd>
                          </div>
                          <div>
                            <dt>Range</dt>
                            <dd className="mono">
                              {signal.min != null || signal.max != null
                                ? `${dash(signal.min)} … ${dash(signal.max)}`
                                : '—'}
                              {unitFor(signal) ? ` ${unitFor(signal)}` : ''}
                            </dd>
                          </div>
                          <div>
                            <dt>Values</dt>
                            <dd>{valuesFor(signal)}</dd>
                          </div>
                          <div>
                            <dt>Grid</dt>
                            <dd>
                              {signal._size === 1
                                ? 'One colored cell in the byte layout above.'
                                : `${signal._size} consecutive colored cells (› start · mid ‹ end).`}
                            </dd>
                          </div>
                        </dl>
                      </div>
                    </td>
                  </tr>
                ) : null}
              </Fragment>
            )
          })}
        </tbody>
      </table>
    </div>
  )
}

/* ── Message card (old structure) ─────────────────────────────────── */

function MessageCard({ message }: { message: DictMessage }) {
  const [activeSignal, setActiveSignal] = useState(-1)
  const [expandedSignal, setExpandedSignal] = useState(-1)
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

      <div className="route-map" aria-label={`${message.name} sender and receivers`}>
        <span className="route-node tx">TX {message.sender}</span>
        <span className="route-arrow" aria-hidden>
          →
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
        {message.comment ? <p className="message-comment">{message.comment}</p> : null}
        <BitGrid
          message={message}
          activeSignal={activeSignal}
          setActiveSignal={setActiveSignal}
          pinnedSignal={expandedSignal}
        />
        <SignalTable
          message={message}
          activeSignal={activeSignal}
          setActiveSignal={setActiveSignal}
          expandedSignal={expandedSignal}
          setExpandedSignal={setExpandedSignal}
        />
      </section>
    </article>
  )
}

/* ── Workspace ────────────────────────────────────────────────────── */

export function CanDictionary() {
  const [messages, setMessages] = useState<DictMessage[]>([])
  const [hash, setHash] = useState('')
  const [source, setSource] = useState('')
  const [busFilter, setBusFilter] = useState<BusFilter>('all')
  const [filterText, setFilterText] = useState('')
  const [err, setErr] = useState('')
  const [busy, setBusy] = useState(false)
  const [loadedAt, setLoadedAt] = useState('')

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
          <span className="muted">Hover bits · click a signal row to expand</span>
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
