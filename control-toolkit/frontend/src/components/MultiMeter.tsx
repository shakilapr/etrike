import { Fragment, useMemo } from 'react'

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
  targetValue?: number | null
  subLeft?: MultiMeterSignal
  subRight?: MultiMeterSignal
  subColumns?: MultiMeterSignal[]
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
  targetValue,
  subLeft,
  subRight,
  subColumns,
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

  // Target tick & marker ('T') calculation
  const targetElements = useMemo(() => {
    if (targetValue == null || !Number.isFinite(targetValue)) return null
    let tFrac: number
    if (bipolar) {
      const bound = Math.max(Math.abs(min), Math.abs(max)) || 1
      tFrac = (targetValue + bound) / (2 * bound)
    } else {
      tFrac = (targetValue - min) / (max - min || 1)
    }
    tFrac = Math.max(0, Math.min(1, tFrac))

    const deg = 180 - tFrac * 180
    const rad = (deg * Math.PI) / 180

    // Radial tick line extending across the arc (R = 124)
    const rInner = R - 12
    const rOuter = R + 12
    const x1 = CX + rInner * Math.cos(rad)
    const y1 = CY - rInner * Math.sin(rad)
    const x2 = CX + rOuter * Math.cos(rad)
    const y2 = CY - rOuter * Math.sin(rad)

    // Position for the 'T' letter badge placed cleanly inside the arc
    const rText = R - 22
    const tx = CX + rText * Math.cos(rad)
    const ty = CY - rText * Math.sin(rad)

    return { x1, y1, x2, y2, tx, ty }
  }, [targetValue, min, max, bipolar])

  const subCols = useMemo(() => {
    if (subColumns && subColumns.length > 0) {
      return subColumns
    }
    const cols: MultiMeterSignal[] = []
    if (subLeft) cols.push(subLeft)
    if (subRight) cols.push(subRight)
    return cols
  }, [subColumns, subLeft, subRight])

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
                className={`protocol-status-tag tone-${protocolAudit.status}`}
                title={protocolAudit.note ?? `Protocol verification: ${protocolAudit.status}`}
                data-testid={`${testId}-protocol-pill`}
              >
                <span>
                  {protocolAudit.status === 'conforming'
                    ? '✓ OK'
                    : protocolAudit.status === 'warning'
                      ? '⚠ Warn'
                      : protocolAudit.status === 'error'
                        ? '✕ Fault'
                        : '• Bench'}
                </span>
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

          {/* Target Setpoint Marker ('T' on Arc) */}
          {targetElements && (
            <g className="multi-meter-target-group" aria-label={`Target speed ${targetValue}`}>
              <line
                x1={targetElements.x1}
                y1={targetElements.y1}
                x2={targetElements.x2}
                y2={targetElements.y2}
                className="multi-meter-target-tick"
              />
              <text
                x={targetElements.tx}
                y={targetElements.ty}
                className="multi-meter-target-text"
                textAnchor="middle"
                dominantBaseline="central"
              >
                T
              </text>
            </g>
          )}
        </svg>

        {/* Center Digital Readout — Clean Minimalist Aesthetic */}
        <div className="multi-meter-center-readout">
          <div
            className="multi-meter-center-label"
            data-testid={`${testId}-center-label`}
            title={primary.varName ? `${primary.label} (${primary.varName}${primary.canId ? ` · CAN ${primary.canId}` : ''})` : primary.canId ? `CAN ${primary.canId}` : undefined}
          >
            <span>{primary.label}</span>
          </div>
          <div className="multi-meter-center-value" data-testid={`${testId}-center-value`}>
            {pNum != null ? pNum.toFixed(pDigits) : '—'}
          </div>
          <div className="multi-meter-center-unit">{primary.unit}</div>
        </div>
      </div>

      {/* Sub-meters Split Strip */}
      {subCols.length > 0 && (
        <div
          className={`multi-meter-sub-strip cols-${subCols.length}`}
          data-testid={`${testId}-sub-strip`}
        >
          {subCols.map((col, idx) => (
            <Fragment key={idx}>
              {idx > 0 && <div className="multi-meter-sub-divider" aria-hidden="true" />}
              <div
                className="multi-meter-sub-col"
                data-testid={
                  idx === 0
                    ? `${testId}-sub-left`
                    : idx === 1 && subCols.length === 2
                      ? `${testId}-sub-right`
                      : `${testId}-sub-${idx}`
                }
              >
                <div
                  className="multi-meter-sub-label-row"
                  title={
                    col.varName
                      ? `${col.label} (${col.varName}${col.canId ? ` · CAN ${col.canId}` : ''})`
                      : col.canId
                        ? `CAN ${col.canId}`
                        : undefined
                  }
                >
                  <span className="multi-meter-sub-label">{col.label}</span>
                </div>
                <span className="multi-meter-sub-value">
                  {formatSub(col.value, col.digits ?? pDigits)}
                </span>
                <span className="multi-meter-sub-unit">{col.unit}</span>
              </div>
            </Fragment>
          ))}
        </div>
      )}

      {/* Integrated Combined Secondary Metrics (e.g. Slew/Torque for Steering, Stroke for Brake) */}
      {secondaryMetrics && secondaryMetrics.length > 0 && (
        <div className="multi-meter-integrated-strip" data-testid={`${testId}-integrated-strip`}>
          {secondaryMetrics.map((sm, idx) => (
            <div
              key={idx}
              className="multi-meter-integrated-item"
              title={sm.varName ? `${sm.label} (${sm.varName}${sm.canId ? ` · CAN ${sm.canId}` : ''})` : sm.label}
            >
              <span className="integrated-label">{sm.label}</span>
              <span className="integrated-value mono">{sm.value}</span>
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
