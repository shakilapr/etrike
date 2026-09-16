import { useMemo } from 'react'

export type SecondaryMetric = {
  label: string
  value: string | number
  canId?: string
  varName?: string
}

export type MultiMeterSignal = {
  label: string
  value: number | null | undefined
  unit: string
  digits?: number
  canId?: string
  varName?: string
}

export type MultiMeterProps = {
  title?: string
  subtitle?: string
  badge?: string
  badgeTone?: 'ok' | 'warn' | 'danger' | 'info' | 'muted'
  primary: MultiMeterSignal
  subLeft: MultiMeterSignal
  subRight: MultiMeterSignal
  secondaryMetrics?: SecondaryMetric[]
  min?: number
  max?: number
  ticks?: number[]
  bipolar?: boolean
  testId?: string
  tone?: 'default' | 'ok' | 'warn' | 'danger'
  footerNote?: string
  protocolAudit?: {
    status: 'conforming' | 'warning' | 'error' | 'unverified'
    note?: string
  }
}

const R = 124
const CX = 175
const CY = 160
const ARC_SPAN_DEG = 180
const ARC_LEN = Math.PI * R

export function MultiMeter({
  title,
  subtitle,
  badge,
  badgeTone = 'info',
  primary,
  subLeft,
  subRight,
  secondaryMetrics,
  min = 0,
  max = 200,
  ticks = [0, 50, 100, 150, 200],
  bipolar = false,
  testId = 'multi-meter',
  tone = 'default',
  footerNote,
  protocolAudit,
}: MultiMeterProps) {
  const pVal = primary.value
  const pNum = typeof pVal === 'number' && Number.isFinite(pVal) ? pVal : null
  const pDigits = primary.digits ?? (bipolar ? 1 : 0)

  // Clamped fraction [0, 1] for unipolar, [-1, 1] for bipolar
  const fraction = useMemo(() => {
    if (pNum == null) return 0
    if (bipolar) {
      const bound = Math.max(Math.abs(min), Math.abs(max)) || 1
      const clamped = Math.max(-bound, Math.min(bound, pNum))
      return clamped / bound
    }
    const span = max - min || 1
    const clamped = Math.max(min, Math.min(max, pNum))
    return (clamped - min) / span
  }, [pNum, min, max, bipolar])

  // Stroke Dash calculations for the active arc
  const { strokeDasharray, strokeDashoffset, tipPos } = useMemo(() => {
    if (bipolar) {
      const halfArc = ARC_LEN / 2
      const fillLen = Math.abs(fraction) * halfArc
      const angleDeg = 90 - fraction * 90
      const angleRad = (angleDeg * Math.PI) / 180
      const tipX = CX + R * Math.cos(angleRad)
      const tipY = CY - R * Math.sin(angleRad)

      const offset = fraction >= 0 ? halfArc : halfArc - fillLen
      return {
        strokeDasharray: `${fillLen} ${ARC_LEN * 2}`,
        strokeDashoffset: -offset,
        tipPos: { x: tipX, y: tipY, angleRad },
      }
    }

    const angleDeg = 180 - fraction * ARC_SPAN_DEG
    const angleRad = (angleDeg * Math.PI) / 180
    const tipX = CX + R * Math.cos(angleRad)
    const tipY = CY - R * Math.sin(angleRad)

    return {
      strokeDasharray: `${ARC_LEN}`,
      strokeDashoffset: ARC_LEN * (1 - Math.max(0, Math.min(1, fraction))),
      tipPos: { x: tipX, y: tipY, angleRad },
    }
  }, [fraction, bipolar])

  // Scale ticks along arc
  const tickElements = useMemo(() => {
    const list: Array<{
      value: number
      label: string
      x1: number
      y1: number
      x2: number
      y2: number
      lx: number
      ly: number
    }> = []

    for (const t of ticks) {
      let tFrac: number
      if (bipolar) {
        const bound = Math.max(Math.abs(min), Math.abs(max)) || 1
        tFrac = (t + bound) / (2 * bound)
      } else {
        tFrac = (t - min) / (max - min || 1)
      }
      tFrac = Math.max(0, Math.min(1, tFrac))

      const deg = 180 - tFrac * 180
      const rad = (deg * Math.PI) / 180

      const innerR = R - 14
      const tickLen = 8
      const x1 = CX + (innerR - tickLen) * Math.cos(rad)
      const y1 = CY - (innerR - tickLen) * Math.sin(rad)
      const x2 = CX + innerR * Math.cos(rad)
      const y2 = CY - innerR * Math.sin(rad)

      const labelR = R + 22
      const lx = CX + labelR * Math.cos(rad)
      const ly = CY - labelR * Math.sin(rad)

      list.push({
        value: t,
        label: String(t),
        x1,
        y1,
        x2,
        y2,
        lx,
        ly,
      })
    }
    return list
  }, [ticks, min, max, bipolar])

  // Intermediate minor ticks
  const minorTicks = useMemo(() => {
    const items: Array<{ x1: number; y1: number; x2: number; y2: number }> = []
    const count = 16
    for (let i = 1; i < count; i++) {
      const frac = i / count
      const deg = 180 - frac * 180
      const rad = (deg * Math.PI) / 180
      const innerR = R - 14
      const tickLen = 4
      const x1 = CX + (innerR - tickLen) * Math.cos(rad)
      const y1 = CY - (innerR - tickLen) * Math.sin(rad)
      const x2 = CX + innerR * Math.cos(rad)
      const y2 = CY - innerR * Math.sin(rad)
      items.push({ x1, y1, x2, y2 })
    }
    return items
  }, [])

  // Tip tick calculation (needle tip)
  const tipTick = useMemo(() => {
    if (pNum == null) return null
    const { angleRad } = tipPos
    const rInner = R - 10
    const rOuter = R + 10
    return {
      x1: CX + rInner * Math.cos(angleRad),
      y1: CY - rInner * Math.sin(angleRad),
      x2: CX + rOuter * Math.cos(angleRad),
      y2: CY - rOuter * Math.sin(angleRad),
    }
  }, [pNum, tipPos])

  const formatSub = (v: number | null | undefined, digits = 0) => {
    if (typeof v === 'number' && Number.isFinite(v)) {
      return v.toFixed(digits)
    }
    return '—'
  }

  return (
    <div
      className={`multi-meter-card tone-${tone}`}
      data-testid={testId}
      aria-label={`${title ?? primary.label} digital meter`}
    >
      {/* Header with Title and subtle Status Indicators */}
      {(title || badge || protocolAudit) && (
        <div className="multi-meter-header">
          <div className="flex items-center gap-2">
            {title && <span className="multi-meter-title">{title}</span>}
            {subtitle && <span className="multi-meter-subtitle">{subtitle}</span>}
          </div>
          <div className="flex items-center gap-1.5">
            {protocolAudit && (
              <span
                className={`protocol-pill tone-${protocolAudit.status}`}
                title={protocolAudit.note ?? `Protocol verification: ${protocolAudit.status}`}
                data-testid={`${testId}-protocol-pill`}
              >
                <span className="protocol-dot" />
                {protocolAudit.status === 'conforming'
                  ? '✓'
                  : protocolAudit.status === 'warning'
                    ? '!'
                    : protocolAudit.status === 'error'
                      ? '✕'
                      : '?'}
              </span>
            )}
            {badge && (
              <span className={`status-pill tone-${badgeTone} text-xs font-semibold px-2 py-0.5 rounded`}>
                {badge}
              </span>
            )}
          </div>
        </div>
      )}

      {/* Main Gauge SVG */}
      <div className="multi-meter-gauge-wrap">
        <svg
          viewBox="0 0 350 220"
          className="multi-meter-svg"
          preserveAspectRatio="xMidYMid meet"
        >
          {/* Background Arc Track */}
          <path
            d={`M ${CX - R} ${CY} A ${R} ${R} 0 0 1 ${CX + R} ${CY}`}
            fill="none"
            className="multi-meter-track"
          />

          {/* Scale Ticks (Minor) */}
          {minorTicks.map((mt, idx) => (
            <line
              key={`minor-${idx}`}
              x1={mt.x1}
              y1={mt.y1}
              x2={mt.x2}
              y2={mt.y2}
              className="multi-meter-tick-minor"
            />
          ))}

          {/* Scale Ticks (Major) */}
          {tickElements.map((t) => (
            <line
              key={`tick-${t.value}`}
              x1={t.x1}
              y1={t.y1}
              x2={t.x2}
              y2={t.y2}
              className="multi-meter-tick-major"
            />
          ))}

          {/* Numeric Tick Labels */}
          {tickElements.map((t) => (
            <text
              key={`label-${t.value}`}
              x={t.lx}
              y={t.ly}
              className="multi-meter-tick-text"
              textAnchor="middle"
              dominantBaseline="middle"
            >
              {t.label}
            </text>
          ))}

          {/* Active Arc Progress */}
          {pNum != null && (
            <path
              d={`M ${CX - R} ${CY} A ${R} ${R} 0 0 1 ${CX + R} ${CY}`}
              fill="none"
              className="multi-meter-active-arc"
              style={{
                strokeDasharray,
                strokeDashoffset,
              }}
            />
          )}

          {/* Tip Needle */}
          {tipTick && (
            <line
              x1={tipTick.x1}
              y1={tipTick.y1}
              x2={tipTick.x2}
              y2={tipTick.y2}
              className="multi-meter-tip-needle"
            />
          )}
        </svg>

        {/* Center Digital Readout — Clean Minimalist Aesthetic */}
        <div className="multi-meter-center-readout">
          <div className="multi-meter-center-label" data-testid={`${testId}-center-label`}>
            <span>{primary.label}</span>
            {primary.canId && (
              <span
                className="can-id-tag"
                title={primary.varName ?? primary.canId}
                data-testid={`${testId}-primary-can`}
              >
                {primary.canId}
              </span>
            )}
          </div>
          <div className="multi-meter-center-value" data-testid={`${testId}-center-value`}>
            {pNum != null ? pNum.toFixed(pDigits) : '—'}
          </div>
          <div className="multi-meter-center-unit">{primary.unit}</div>
        </div>
      </div>

      {/* Sub-meters Split Strip */}
      <div className="multi-meter-sub-strip" data-testid={`${testId}-sub-strip`}>
        {/* Left Sub-Meter */}
        <div className="multi-meter-sub-col" data-testid={`${testId}-sub-left`}>
          <div className="multi-meter-sub-label-row">
            <span className="multi-meter-sub-label">{subLeft.label}</span>
            {subLeft.canId && (
              <span
                className="can-id-tag"
                title={subLeft.varName ?? subLeft.canId}
                data-testid={`${testId}-sub-left-can`}
              >
                {subLeft.canId}
              </span>
            )}
          </div>
          <span className="multi-meter-sub-value">
            {formatSub(subLeft.value, subLeft.digits ?? pDigits)}
          </span>
          <span className="multi-meter-sub-unit">{subLeft.unit}</span>
        </div>

        {/* Vertical Divider Line */}
        <div className="multi-meter-sub-divider" aria-hidden="true" />

        {/* Right Sub-Meter */}
        <div className="multi-meter-sub-col" data-testid={`${testId}-sub-right`}>
          <div className="multi-meter-sub-label-row">
            <span className="multi-meter-sub-label">{subRight.label}</span>
            {subRight.canId && (
              <span
                className="can-id-tag"
                title={subRight.varName ?? subRight.canId}
                data-testid={`${testId}-sub-right-can`}
              >
                {subRight.canId}
              </span>
            )}
          </div>
          <span className="multi-meter-sub-value">
            {formatSub(subRight.value, subRight.digits ?? pDigits)}
          </span>
          <span className="multi-meter-sub-unit">{subRight.unit}</span>
        </div>
      </div>

      {/* Integrated Combined Secondary Metrics (e.g. Slew/Torque for Steering, Stroke for Brake) */}
      {secondaryMetrics && secondaryMetrics.length > 0 && (
        <div className="multi-meter-integrated-strip" data-testid={`${testId}-integrated-strip`}>
          {secondaryMetrics.map((sm, idx) => (
            <div key={idx} className="multi-meter-integrated-item" title={sm.varName ?? sm.label}>
              <span className="integrated-label">{sm.label}</span>
              <span className="integrated-value mono">{sm.value}</span>
              {sm.canId && (
                <span className="can-id-tag" title={sm.varName ?? sm.canId}>
                  {sm.canId}
                </span>
              )}
            </div>
          ))}
        </div>
      )}

      {/* Optional Footer Note */}
      {footerNote && (
        <div className="multi-meter-footer-note" data-testid={`${testId}-note`}>
          {footerNote}
        </div>
      )}
    </div>
  )
}
