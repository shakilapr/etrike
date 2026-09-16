import { useMemo } from 'react'

export type BrakeMeterProps = {
  title?: string
  badge?: string
  badgeTone?: 'ok' | 'warn' | 'danger' | 'info' | 'muted'
  actualPressure: number | null | undefined
  hostPressure: number | null | undefined
  rtPressure: number | null | undefined
  actualStroke?: number | null | undefined
  reqStroke?: number | null | undefined
  diagStroke?: number | null | undefined
  maxPressure?: number
  maxStroke?: number
  compact?: boolean
  testId?: string
  protocolAudit?: {
    status: 'conforming' | 'warning' | 'error' | 'unverified'
    note?: string
  }
}

export function BrakeMeter({
  title = 'Braking',
  badge = 'Hydraulic Clamping',
  badgeTone = 'ok',
  actualPressure,
  hostPressure,
  rtPressure,
  actualStroke,
  reqStroke,
  diagStroke,
  maxPressure = 5000,
  maxStroke = 50,
  compact = false,
  testId = 'meter-brake',
  protocolAudit,
}: BrakeMeterProps) {
  const CX = 175
  const CY = compact ? 134 : 155
  const R = compact ? 98 : 124
  const ARC_LEN = Math.PI * R

  const pressNum =
    typeof actualPressure === 'number' && Number.isFinite(actualPressure)
      ? actualPressure
      : null

  const strokeNum =
    typeof actualStroke === 'number' && Number.isFinite(actualStroke)
      ? actualStroke
      : null

  const reqStrokeNum =
    typeof reqStroke === 'number' && Number.isFinite(reqStroke)
      ? reqStroke
      : null

  // Clamped pressure fraction [0, 1]
  const pressFrac = useMemo(() => {
    if (pressNum == null) return 0
    return Math.max(0, Math.min(1, pressNum / maxPressure))
  }, [pressNum, maxPressure])

  // Stroke percentage of mechanical travel [0, 100]
  const strokePct = useMemo(() => {
    if (strokeNum == null) return 0
    return Math.max(0, Math.min(100, (strokeNum / maxStroke) * 100))
  }, [strokeNum, maxStroke])

  const reqStrokePct = useMemo(() => {
    if (reqStrokeNum == null) return null
    return Math.max(0, Math.min(100, (reqStrokeNum / maxStroke) * 100))
  }, [reqStrokeNum, maxStroke])

  // Pressure Arc Calculations
  const { strokeDasharray, strokeDashoffset, tipTick } = useMemo(() => {
    const angleDeg = 180 - pressFrac * 180
    const angleRad = (angleDeg * Math.PI) / 180

    const rInner = R - (compact ? 7 : 10)
    const rOuter = R + (compact ? 7 : 10)
    const tip =
      pressNum != null
        ? {
            x1: CX + rInner * Math.cos(angleRad),
            y1: CY - rInner * Math.sin(angleRad),
            x2: CX + rOuter * Math.cos(angleRad),
            y2: CY - rOuter * Math.sin(angleRad),
          }
        : null

    return {
      strokeDasharray: `${ARC_LEN}`,
      strokeDashoffset: ARC_LEN * (1 - pressFrac),
      tipTick: tip,
    }
  }, [pressFrac, pressNum, R, CY, ARC_LEN, compact])

  // Scale Ticks (0, 1k, 2k, 3k, 4k, 5k)
  const ticks = useMemo(() => {
    const list: Array<{ val: number; label: string; x1: number; y1: number; x2: number; y2: number; lx: number; ly: number }> = []
    const stepList = compact ? [0, 2500, 5000] : [0, 1000, 2000, 3000, 4000, 5000]

    for (const val of stepList) {
      const frac = val / maxPressure
      const deg = 180 - frac * 180
      const rad = (deg * Math.PI) / 180

      const tickLen = val === 0 || val === maxPressure ? (compact ? 9 : 12) : (compact ? 6 : 8)
      const x1 = CX + (R - tickLen) * Math.cos(rad)
      const y1 = CY - (R - tickLen) * Math.sin(rad)
      const x2 = CX + (R + 2) * Math.cos(rad)
      const y2 = CY - (R + 2) * Math.sin(rad)

      const labelR = R + (compact ? 12 : 16)
      const lx = CX + labelR * Math.cos(rad)
      const ly = CY - labelR * Math.sin(rad)

      let label = `${val / 1000}k`
      if (val === 0) label = '0'

      list.push({ val, label, x1, y1, x2, y2, lx, ly })
    }
    return list
  }, [maxPressure, R, CY, compact])

  // Minor ticks
  const minorTicks = useMemo(() => {
    if (compact) return []
    const list: Array<{ x1: number; y1: number; x2: number; y2: number }> = []
    for (let p = 500; p < maxPressure; p += 1000) {
      const frac = p / maxPressure
      const deg = 180 - frac * 180
      const rad = (deg * Math.PI) / 180
      const tickLen = 4
      list.push({
        x1: CX + (R - tickLen) * Math.cos(rad),
        y1: CY - (R - tickLen) * Math.sin(rad),
        x2: CX + R * Math.cos(rad),
        y2: CY - R * Math.sin(rad),
      })
    }
    return list
  }, [maxPressure, R, CY, compact])

  // Clamping state badge
  const clampingStateText =
    pressNum == null
      ? 'STANDBY'
      : pressNum < 50
        ? 'RELEASED'
        : pressNum > 3500
          ? 'EMERGENCY CLAMP'
          : `CLAMPING ${pressNum.toFixed(0)} kPa`

  return (
    <div
      className={`multi-meter-card tone-default brake-meter-card ${compact ? 'is-compact' : ''}`}
      data-testid={testId}
      aria-label="Hydraulic braking and mechanical caliper stroke meter"
    >
      {/* Card Header */}
      <div className="multi-meter-header">
        <div className="flex items-center gap-2">
          <span className="multi-meter-title">{title}</span>
          <span className="multi-meter-subtitle">
            {compact ? 'Host → RT → SYS → SEB' : 'Hydraulic & Caliper Travel'}
          </span>
        </div>
        <div className="flex items-center gap-1.5">
          {protocolAudit && (
            <span
              className={`protocol-pill tone-${protocolAudit.status}`}
              title={protocolAudit.note ?? `Protocol verification: ${protocolAudit.status}`}
              data-testid={`${testId}-protocol-pill`}
            >
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

      {/* Hydraulic Pressure Arc & Center Value */}
      <div className="multi-meter-gauge-wrap brake-gauge-wrap">
        <svg
          viewBox={compact ? "0 0 350 162" : "0 0 350 215"}
          className="multi-meter-svg brake-svg"
          preserveAspectRatio="xMidYMid meet"
        >
          {/* Baseline Pressure Track */}
          <path
            d={`M ${CX - R} ${CY} A ${R} ${R} 0 0 1 ${CX + R} ${CY}`}
            fill="none"
            className="multi-meter-track"
          />

          {/* Minor Ticks */}
          {minorTicks.map((mt, idx) => (
            <line
              key={`bm-${idx}`}
              x1={mt.x1}
              y1={mt.y1}
              x2={mt.x2}
              y2={mt.y2}
              className="multi-meter-tick-minor"
            />
          ))}

          {/* Major Ticks */}
          {ticks.map((t) => (
            <line
              key={`bt-${t.val}`}
              x1={t.x1}
              y1={t.y1}
              x2={t.x2}
              y2={t.y2}
              className="multi-meter-tick-major"
            />
          ))}

          {/* Numeric Tick Labels */}
          {ticks.map((t) => (
            <text
              key={`btl-${t.val}`}
              x={t.lx}
              y={t.ly}
              className="multi-meter-tick-text"
              textAnchor="middle"
              dominantBaseline="middle"
            >
              {t.label}
            </text>
          ))}

          {/* Active Hydraulic Arc Progress */}
          {pressNum != null && (
            <path
              d={`M ${CX - R} ${CY} A ${R} ${R} 0 0 1 ${CX + R} ${CY}`}
              fill="none"
              className={`brake-active-arc ${pressNum > 3500 ? 'is-danger' : pressNum > 500 ? 'is-active' : ''}`}
              style={{
                strokeDasharray,
                strokeDashoffset,
              }}
            />
          )}

          {/* Current Pressure Tip Marker */}
          {tipTick && (
            <line
              x1={tipTick.x1}
              y1={tipTick.y1}
              x2={tipTick.x2}
              y2={tipTick.y2}
              className="multi-meter-tip-tick"
            />
          )}
        </svg>

        {/* Center Readout: Clamping Pressure */}
        <div className="multi-meter-center-readout brake-center-readout">
          <div className="brake-clamp-status-tag">{clampingStateText}</div>
          <div className="multi-meter-center-value" data-testid={`${testId}-center-value`}>
            {pressNum != null ? pressNum.toFixed(0) : '—'}
          </div>
          <div className="multi-meter-center-unit">kPa</div>
          <div className="multi-meter-center-label" data-testid={`${testId}-center-label`}>
            <span>ACTUAL BRAKE</span>
            <span
              className="can-id-tag"
              title="SEB_STATUS: pressure_kpa (Low 0x721)"
              data-testid={`${testId}-primary-can`}
            >
              0x721
            </span>
          </div>
        </div>
      </div>

      {/* Integrated Caliper Mechanical Stroke Bar (Stage 3 SYS Req vs Stage 4 Actual) */}
      <div className="brake-stroke-container" data-testid={`${testId}-stroke-bar`}>
        <div className="brake-stroke-header">
          <div className="brake-stroke-label">
            <span>CALIPER STROKE</span>
            <span
              className="can-id-tag ml-1"
              title="SEB_STATUS: stroke_mm (Low 0x721)"
              data-testid={`${testId}-stroke-can`}
            >
              0x721
            </span>
          </div>
          <div className="brake-stroke-value">
            <span className="font-bold">{strokeNum != null ? strokeNum.toFixed(1) : '—'}</span>
            <span className="text-muted text-xs"> / {maxStroke} mm</span>
          </div>
        </div>

        {/* Linear Travel Bar with Target Needle */}
        <div className="brake-stroke-track">
          <div
            className="brake-stroke-fill"
            style={{ width: `${strokePct}%` }}
            title={`Caliper stroke: ${strokeNum != null ? strokeNum.toFixed(1) : 0} mm`}
          />
          {reqStrokePct != null && (
            <div
              className="brake-stroke-req-marker"
              style={{ left: `${reqStrokePct}%` }}
              title={`SYS Requested Stroke: ${reqStrokeNum?.toFixed(1)} mm (0x7B9)`}
            />
          )}
        </div>
      </div>

      {/* Sub-meters Split Strip: Stage 1 (Host 0x301) vs Stage 2 (RT 0x205) */}
      <div className="multi-meter-sub-strip" data-testid={`${testId}-sub-strip`}>
        {/* Left Sub-Meter: Host Command */}
        <div className="multi-meter-sub-col" data-testid={`${testId}-sub-left`}>
          <div className="multi-meter-sub-label-row">
            <span className="multi-meter-sub-label">HOST BRAKE</span>
            <span
              className="can-id-tag"
              title="HOST_BRAKE_REQ: brake_pressure_kpa (High 0x301)"
              data-testid={`${testId}-sub-left-can`}
            >
              0x301
            </span>
          </div>
          <span className="multi-meter-sub-value">
            {typeof hostPressure === 'number' && Number.isFinite(hostPressure)
              ? hostPressure.toFixed(0)
              : '—'}
          </span>
          <span className="multi-meter-sub-unit">kPa Demand</span>
        </div>

        {/* Vertical Divider */}
        <div className="multi-meter-sub-divider" aria-hidden="true" />

        {/* Right Sub-Meter: RT Command */}
        <div className="multi-meter-sub-col" data-testid={`${testId}-sub-right`}>
          <div className="multi-meter-sub-label-row">
            <span className="multi-meter-sub-label">RT BRAKE</span>
            <span
              className="can-id-tag"
              title="RT_BRAKE_CMD: brake_pressure_kpa (Low 0x205)"
              data-testid={`${testId}-sub-right-can`}
            >
              0x205
            </span>
          </div>
          <span className="multi-meter-sub-value">
            {typeof rtPressure === 'number' && Number.isFinite(rtPressure)
              ? rtPressure.toFixed(0)
              : '—'}
          </span>
          <span className="multi-meter-sub-unit">kPa Setpoint</span>
        </div>
      </div>

      {/* Secondary Diag & SYS Request Row */}
      {!compact && (
        <div className="multi-meter-integrated-strip" data-testid={`${testId}-diag-strip`}>
          <div className="multi-meter-integrated-item" title="SEB_STATUS: stroke_mm (Low 0x721)">
            <div className="integrated-label-row">
              <span className="integrated-label">Actual Stroke</span>
              <span className="can-id-tag">0x721</span>
            </div>
            <span className="integrated-value mono">
              {strokeNum != null ? `${strokeNum.toFixed(1)} mm` : '—'}
            </span>
          </div>

          <div className="multi-meter-integrated-item" title="VCU_SEB_REQ: stroke_mm (Low 0x7B9 via SYS)">
            <div className="integrated-label-row">
              <span className="integrated-label">SYS Req</span>
              <span className="can-id-tag">0x7B9</span>
            </div>
            <span className="integrated-value mono">
              {reqStrokeNum != null ? `${reqStrokeNum.toFixed(1)} mm` : '—'}
            </span>
          </div>

          <div className="multi-meter-integrated-item" title="BRAKE_DIAG: pressure_raw (High 0x311)">
            <div className="integrated-label-row">
              <span className="integrated-label">Diag Raw</span>
              <span className="can-id-tag">0x311</span>
            </div>
            <span className="integrated-value mono">
              {typeof diagStroke === 'number' && Number.isFinite(diagStroke)
                ? `${diagStroke.toFixed(1)} mm`
                : '—'}
            </span>
          </div>
        </div>
      )}
    </div>
  )
}
