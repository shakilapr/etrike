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
  if (f === 1 && o === 0) return '1:1 (raw = eng)'
  if (f === 1) return `eng = raw ${o >= 0 ? '+' : '−'} ${Math.abs(o)}`
  if (o === 0) return `eng = raw × ${f}`
  return `eng = raw × ${f} ${o >= 0 ? '+' : '−'} ${Math.abs(o)}`
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
  if (/_mm$/i.test(signal.key)) return 'mm'
  return ''
}

function titleFor(signal: DictField): string {
  const k = signal.key
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

/** Curated expand-panel docs. Keep good copy; only fill gaps with smart fallbacks. */
type SignalDoc = {
  /** What the signal is (kept for keys that already had good MEANING). */
  meaning: string
  /** Why the field exists / who consumes it. */
  why: string
  /** Human data-type description. */
  dataType: string
  /** Concrete wire / engineering examples. */
  examples: string[]
  /** Optional per-bit notes for true bitfields (index 0 = LSB of field). */
  bits?: string[]
}

const SIGNAL_DOCS: Record<string, SignalDoc> = {
  speed_mmps: {
    meaning: 'Host longitudinal speed command for RT kinematics (mm/s).',
    why: 'RT shapes vehicle motion from Host intent on High bus (not a direct motor PWM).',
    dataType: 'Signed integer speed command (engineering mm/s).',
    examples: [
      '0 → standstill',
      '1000 → ~1 m/s forward',
      '3000 → firmware max forward',
      '−500 → max reverse command band',
    ],
  },
  yaw_rate_mrad_s: {
    meaning: 'Host yaw-rate command for RT kinematics (mrad/s).',
    why: 'Pairs with speed so RT can run bicycle/tricycle kinematics (turn while moving).',
    dataType: 'Signed integer yaw rate (milliradians per second).',
    examples: [
      '0 → straight',
      '1000 → gentle left/right yaw (sign = direction)',
      '±3000 → firmware clamp',
    ],
  },
  gear: {
    meaning: 'Requested gear / drive mode on the motion command.',
    why: 'Tells RT/MTR which motion regime to honor (N/D/S/R).',
    dataType: 'Unsigned enum (8-bit storage, 4 legal values).',
    examples: ['0 = N (neutral)', '1 = D (drive)', '2 = S (sport / higher authority)', '3 = R (reverse)'],
  },
  gear_state: {
    meaning: 'Gear currently reported by the motor controller.',
    why: 'Feedback path so Host/RT can confirm requested gear took effect.',
    dataType: 'Unsigned integer gear report (same 0–3 map as command when used).',
    examples: ['0 = N', '1 = D', '2 = S', '3 = R (if mapped the same way)'],
  },
  motor_speed_mmps: {
    meaning: 'Direct motor speed on Low bus (bypasses Host kinematics).',
    why: 'Bench / direct-actuator path talks to the motor without High-bus HOST_DRIVE_CMD.',
    dataType: 'Signed integer motor speed command (mm/s).',
    examples: ['0 stop', '1500 forward crawl', 'negative = reverse'],
  },
  actual_speed_mmps: {
    meaning: 'Measured motor speed feedback (mm/s).',
    why: 'Closed-loop observation for RT PID / Host monitoring.',
    dataType: 'Signed integer measured speed (mm/s).',
    examples: ['Matches command when tracking well', '0 when stopped', 'sign = direction'],
  },
  estop_active: {
    meaning: 'Vehicle emergency-stop latched (1 = ESTOP active).',
    why: 'Safety state shared so all nodes freeze motion when latched.',
    dataType: 'Boolean-like unsigned (0/1, often full byte).',
    examples: ['0 = motion allowed', '1 = ESTOP latched — stop / no drive'],
  },
  heartbeat_ok: {
    meaning: 'Safety heartbeat healthy flag from SYS.',
    why: 'If the safety node stops ticking healthy, peers treat the link as degraded.',
    dataType: 'Boolean-like unsigned (0/1).',
    examples: ['1 = SYS heartbeat healthy', '0 = heartbeat failed / not OK'],
  },
  light_left: {
    meaning: 'Left turn / indicator lamp bit.',
    why: 'Lamp status mirror on safety/status frames.',
    dataType: 'Single flag bit (0/1).',
    examples: ['0 = off', '1 = left indicator on'],
  },
  light_right: {
    meaning: 'Right turn / indicator lamp bit.',
    why: 'Lamp status mirror on safety/status frames.',
    dataType: 'Single flag bit (0/1).',
    examples: ['0 = off', '1 = right indicator on'],
  },
  light_brake: {
    meaning: 'Brake lamp bit.',
    why: 'Reports brake-light state for status / diagnostics.',
    dataType: 'Single flag bit (0/1).',
    examples: ['0 = off', '1 = brake light on'],
  },
  light_head: {
    meaning: 'Headlamp bit.',
    why: 'Reports headlight state for status / diagnostics.',
    dataType: 'Single flag bit (0/1).',
    examples: ['0 = off', '1 = headlight on'],
  },
  left_turn: {
    meaning: 'Left turn lamp command bit (HOST_LIGHT_CMD).',
    why: 'Host commands exterior lighting; may bridge High→Low same_frame.',
    dataType: 'Single command bit (0/1).',
    examples: ['0 = left off', '1 = left on'],
  },
  right_turn: {
    meaning: 'Right turn lamp command bit (HOST_LIGHT_CMD).',
    why: 'Host exterior light command.',
    dataType: 'Single command bit (0/1).',
    examples: ['0 = right off', '1 = right on'],
  },
  brake_light: {
    meaning: 'Brake lamp command bit (HOST_LIGHT_CMD).',
    why: 'Host exterior light command.',
    dataType: 'Single command bit (0/1).',
    examples: ['0 = brake lamp off', '1 = brake lamp on'],
  },
  headlight: {
    meaning: 'Headlamp command bit (HOST_LIGHT_CMD).',
    why: 'Host exterior light command.',
    dataType: 'Single command bit (0/1).',
    examples: ['0 = headlight off', '1 = headlight on'],
  },
  fault_flags: {
    meaning: 'Motor fault / status bitfield.',
    why: 'Compact motor health so RT/SYS/Host can react without many frames.',
    dataType: 'Unsigned 8-bit bitfield (each bit is a status flag).',
    examples: ['0x00 = no flags', 'non-zero = one or more motor status bits set'],
    bits: [
      'bit0 — general fault / trip (if set by MCU firmware)',
      'bit1 — over-temp / thermal (typical MCU use)',
      'bit2 — over-current',
      'bit3 — under-voltage',
      'bit4–7 — vendor / reserved (treat as opaque unless MCU doc says otherwise)',
    ],
  },
  health_flags: {
    meaning: 'Host health bitfield on heartbeat.',
    why: 'Cheap peer liveness beyond a pure counter.',
    dataType: 'Unsigned 8-bit bitfield.',
    examples: ['0 = nominal', 'non-zero = degraded flags set by Host software'],
  },
  target_angle_raw: {
    meaning: 'Steering target angle in vendor raw units.',
    why: 'SES vendor codec — not SI degrees until scaled by vendor factor.',
    dataType: 'Vendor raw integer (see SES codec / scale).',
    examples: ['0 ≈ center (vendor)', 'positive/negative = left/right in raw ticks'],
  },
  pressure_request_raw: {
    meaning: 'Brake pressure request in vendor raw units.',
    why: 'SEB vendor path; engineering kPa needs vendor scale.',
    dataType: 'Vendor raw integer pressure request.',
    examples: ['0 = no pressure request', 'higher raw → higher request (vendor map)'],
  },
  rolling_counter: {
    meaning: 'Rolling counter for frame freshness.',
    why: 'Receivers detect stuck/duplicate producers when the counter stops advancing.',
    dataType: 'Unsigned wrapping counter (typically 8-bit 0…255).',
    examples: ['… 10, 11, 12 … increments each TX', 'frozen value → stale producer'],
  },
  checksum: {
    meaning: 'Frame integrity checksum.',
    why: 'Rejects corrupted vendor payloads (SES/SEB custom checksums).',
    dataType: 'Unsigned checksum byte(s) per vendor codec.',
    examples: ['Must match codec rule or frame is invalid'],
  },
  req_mode: {
    meaning: 'HMI operating mode request.',
    why: 'Driver interface asks SYS/Host for MANUAL vs AUTO.',
    dataType: 'Unsigned enum (stored in 8 bits).',
    examples: ['0 = MANUAL', '1 = AUTO'],
  },
  req_start: {
    meaning: 'HMI power start/stop request.',
    why: 'Power on/off intent from the HMI to SYS.',
    dataType: 'Unsigned enum (stored in 8 bits).',
    examples: ['0 = OFF', '1 = ON'],
  },
  brake_pressure_kpa: {
    meaning: 'Host brake pressure request in kPa.',
    why: 'High-bus brake intent for RT / brake path (not SEB raw).',
    dataType: 'Signed integer pressure (kPa).',
    examples: ['0 = release', '2000 = light apply', 'up to protocol max'],
  },
  distance_mm: {
    meaning: 'Obstacle distance ahead (mm), or clear sentinel.',
    why: 'Host perception → RT for slowdown / stop decisions.',
    dataType: 'Unsigned distance; special enum for clear.',
    examples: ['1200 = 1.2 m', '4294967295 = clear (no obstacle)'],
  },
  alive_ctr: {
    meaning: 'Host alive counter on heartbeat.',
    why: 'Peers detect Host restart / freeze via wrap and gaps.',
    dataType: 'Unsigned wrapping counter.',
    examples: ['Increments each heartbeat period', 'reset on ECU restart'],
  },
  mode: {
    meaning: 'RT reported operating mode.',
    why: 'Host/SYS observe whether RT is MANUAL, AUTO, or ESTOP.',
    dataType: 'Unsigned enum.',
    examples: ['0 = MANUAL', '1 = AUTO', '2 = ESTOP'],
  },
  safety_state: {
    meaning: 'RT safety state nibble/field.',
    why: 'Compact safety posture next to ESTOP reason.',
    dataType: 'Small unsigned field (2 bits in RT_STATE_RPT).',
    examples: ['0…2 per RT safety mapping'],
  },
  estop_reason: {
    meaning: 'Why ESTOP latched (coded reason).',
    why: 'Diagnostics after a stop — not the stop itself.',
    dataType: 'Unsigned reason code (4 bits on RT_STATE_RPT).',
    examples: ['0 = none / clear', 'non-zero = coded cause'],
  },
  reversing: {
    meaning: 'Vehicle is reversing flag.',
    why: 'Status for lamps / logic that care about reverse motion.',
    dataType: 'Single flag bit.',
    examples: ['0 = not reversing', '1 = reversing'],
  },
  speed_setpoint: {
    meaning: 'RT PID speed setpoint (internal units).',
    why: 'Debug of RT closed-loop command vs measure.',
    dataType: 'Signed 16-bit loop quantity.',
    examples: ['Compare to speed_measured and pid_output on same frame'],
  },
  speed_measured: {
    meaning: 'RT PID measured speed (internal units).',
    why: 'Closed-loop feedback channel for Host analysis.',
    dataType: 'Signed 16-bit loop quantity.',
    examples: ['Tracks setpoint when controller is healthy'],
  },
  pid_output: {
    meaning: 'RT PID controller output (internal units).',
    why: 'Shows actuator effort after the speed loop.',
    dataType: 'Signed 16-bit loop quantity.',
    examples: ['0 ≈ no effort', 'large magnitude = strong correction'],
  },
}

function dataTypeLabel(signal: DictField): string {
  const curated = SIGNAL_DOCS[signal.key]?.dataType
  if (curated) return curated
  const u = unitFor(signal)
  const signed = /signed/i.test(signal._type) || signal._type === 'i16' || signal._type === 'i32'
  if (signal._size === 1 || signal.kind === 'boolean') {
    return 'Flag bit (boolean 0/1 on the wire).'
  }
  if (signal.kind === 'enum' || signal.options?.length) {
    return `Enumeration (${signal._size}-bit ${signal._type || 'unsigned'}${u ? `, ${u}` : ''}).`
  }
  if (signed) {
    return `Signed integer (${signal._size} bits${u ? `, ${u}` : ''}).`
  }
  return `Unsigned integer (${signal._size} bits${u ? `, ${u}` : ''}).`
}

function meaningFor(signal: DictField): string {
  if (SIGNAL_DOCS[signal.key]?.meaning) return SIGNAL_DOCS[signal.key].meaning
  if (signal.kind === 'boolean' || signal._size === 1) {
    return `${titleFor(signal)}: single flag bit (0 = off/false, 1 = on/true).`
  }
  if (signal.kind === 'enum' && signal.options?.length) {
    return `${titleFor(signal)}: enumerated field — ${valuesFor(signal)}.`
  }
  const u = unitFor(signal)
  return u
    ? `${titleFor(signal)}: numeric quantity in ${u}.`
    : `${titleFor(signal)}: ${signal._size}-bit ${signal._type || 'integer'} field.`
}

function whyFor(signal: DictField): string {
  if (SIGNAL_DOCS[signal.key]?.why) return SIGNAL_DOCS[signal.key].why
  if (signal._size === 1) {
    return 'Compact on/off status or command on the bus without a full multi-byte value.'
  }
  if (signal.kind === 'enum' || signal.options?.length) {
    return 'Encodes a small set of named states so nodes agree on meaning without free text.'
  }
  if (/counter/i.test(signal.key)) {
    return 'Lets receivers detect stuck, duplicate, or restarted producers.'
  }
  if (/checksum|crc/i.test(signal.key)) {
    return 'Protects the frame against bit errors on the wire.'
  }
  return 'Carries engineering intent or feedback between ECUs on this message.'
}

function examplesFor(signal: DictField): string[] {
  const curated = SIGNAL_DOCS[signal.key]?.examples
  if (curated?.length) return curated
  const out: string[] = []
  if (signal.options?.length) {
    for (const opt of signal.options.slice(0, 6)) {
      out.push(`${opt.value} = ${opt.label}`)
    }
    return out
  }
  if (signal._size === 1) {
    return ['0 = off / false', '1 = on / true']
  }
  const u = unitFor(signal)
  if (signal.min != null && signal.max != null) {
    out.push(`min ${signal.min}${u ? ` ${u}` : ''}`)
    out.push(`max ${signal.max}${u ? ` ${u}` : ''}`)
  }
  if (signal.min != null && Number(signal.min) <= 0 && Number(signal.max) > 0) {
    out.push(`0${u ? ` ${u}` : ''} = zero / idle (typical)`)
  }
  if (!out.length) {
    out.push(`${signal._size}-bit value interpreted with scale ${scaleFor(signal)}`)
  }
  return out
}

function bitsFor(signal: DictField): string[] | null {
  const curated = SIGNAL_DOCS[signal.key]?.bits
  if (curated?.length) return curated
  // Only invent per-bit rows for true multi-flag bitfields (not plain multi-bit integers).
  if (signal._size > 1 && signal._size <= 8 && /flags?$|mask$/i.test(signal.key)) {
    return Array.from({ length: signal._size }, (_, i) => `bit${i} — vendor/status flag (see ECU doc)`)
  }
  return null
}

function wirePosition(signal: DictField): string {
  const end = signal._bit_offset + signal._size - 1
  const endByte = signal._byte + Math.floor(end / 8)
  const endBit = end % 8
  if (signal._size === 1) {
    return `B${signal._byte}.${signal._bit_offset} (1 bit)`
  }
  return `B${signal._byte}.${signal._bit_offset} → B${endByte}.${endBit} · ${signal._size} bit ${signal._type || 'int'}`
}

/** Compact hover line — no multi-bit packing essay. */
function hoverDetail(signal: DictField, bitInField: number | null): string {
  const bits = bitsFor(signal)
  if (bits && bitInField != null && bits[bitInField]) {
    return bits[bitInField]
  }
  if (signal._size === 1) {
    return `${wirePosition(signal)} · 0/1 flag`
  }
  if (bitInField != null) {
    return `${wirePosition(signal)} · bit ${bitInField} of ${signal._size} (part of one ${dataTypeLabel(signal).replace(/\.$/, '')})`
  }
  return wirePosition(signal)
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
            <p className="bit-inspector-hint mono">{hoverDetail(show, bitInField)}</p>
          </div>
        ) : (
          <div className="bit-inspector-body idle">
            <strong>Bit hover</strong>
            <p>
              Hover a colored cell for signal name + wire position. Click a table row below
              for meaning, why, data type, and examples.
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
                        <dl className="bit-inspector-kv dict-sig-doc">
                          <div>
                            <dt>What</dt>
                            <dd data-testid="dict-expand-what">{meaningFor(signal)}</dd>
                          </div>
                          <div>
                            <dt>Why</dt>
                            <dd data-testid="dict-expand-why">{whyFor(signal)}</dd>
                          </div>
                          <div>
                            <dt>Data type</dt>
                            <dd data-testid="dict-expand-type">{dataTypeLabel(signal)}</dd>
                          </div>
                          <div>
                            <dt>Examples</dt>
                            <dd data-testid="dict-expand-examples">
                              <ul className="dict-sig-examples">
                                {examplesFor(signal).map((ex) => (
                                  <li key={ex}>{ex}</li>
                                ))}
                              </ul>
                            </dd>
                          </div>
                          {bitsFor(signal) ? (
                            <div>
                              <dt>Each bit</dt>
                              <dd data-testid="dict-expand-bits">
                                <ul className="dict-sig-examples">
                                  {bitsFor(signal)!.map((b) => (
                                    <li key={b}>{b}</li>
                                  ))}
                                </ul>
                              </dd>
                            </div>
                          ) : null}
                          <div>
                            <dt>Wire</dt>
                            <dd className="mono">{wirePosition(signal)}</dd>
                          </div>
                          <div>
                            <dt>Scale / range</dt>
                            <dd className="mono">
                              {scaleFor(signal)}
                              {signal.min != null || signal.max != null
                                ? ` · ${dash(signal.min)} … ${dash(signal.max)}`
                                : ''}
                              {unitFor(signal) ? ` ${unitFor(signal)}` : ''}
                            </dd>
                          </div>
                          {signal.options?.length ? (
                            <div>
                              <dt>Enum map</dt>
                              <dd>{valuesFor(signal)}</dd>
                            </div>
                          ) : null}
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
