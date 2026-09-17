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
  badge = 'Standby',
  badgeTone = 'ok',
  actualPressure,
  hostPressure,
  rtPressure,
  actualStroke,
  reqStroke,
  diagStroke: _diagStroke,
  maxPressure = 5000,
  maxStroke = 50,
  compact = false,
  testId = 'meter-brake',
  protocolAudit,
}: BrakeMeterProps) {
  const pressNum =
    typeof actualPressure === 'number' && Number.isFinite(actualPressure)
      ? actualPressure
      : null

  const hostPressNum =
    typeof hostPressure === 'number' && Number.isFinite(hostPressure)
      ? hostPressure
      : null

  const rtPressNum =
    typeof rtPressure === 'number' && Number.isFinite(rtPressure)
      ? rtPressure
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

  const pressPct = pressFrac * 100

  const rtPressPct = useMemo(() => {
    if (rtPressNum == null) return null
    return Math.max(0, Math.min(100, (rtPressNum / maxPressure) * 100))
  }, [rtPressNum, maxPressure])

  const hostPressPct = useMemo(() => {
    if (hostPressNum == null) return null
    return Math.max(0, Math.min(100, (hostPressNum / maxPressure) * 100))
  }, [hostPressNum, maxPressure])

  // Stroke percentage of mechanical travel [0, 100]
  const strokePct = useMemo(() => {
    if (strokeNum == null) return 0
    return Math.max(0, Math.min(100, (strokeNum / maxStroke) * 100))
  }, [strokeNum, maxStroke])

  const reqStrokePct = useMemo(() => {
    if (reqStrokeNum == null) return null
    return Math.max(0, Math.min(100, (reqStrokeNum / maxStroke) * 100))
  }, [reqStrokeNum, maxStroke])

  // Pressure Scale Ticks (0, 1k, 2k, 3k, 4k, 5k)
  const pressureTicks = useMemo(() => {
    const list: Array<{ val: number; label: string; pct: number }> = []
    const stepList = compact ? [0, 2500, 5000] : [0, 1000, 2000, 3000, 4000, 5000]
    for (const val of stepList) {
      list.push({
        val,
        label: val === 0 ? '0' : `${val / 1000}k`,
        pct: (val / maxPressure) * 100,
      })
    }
    return list
  }, [maxPressure, compact])

  // Clamping state badge
  const clampingStateText =
    pressNum == null
      ? 'STANDBY'
      : pressNum < 50
        ? 'RELEASED'
        : pressNum > 3500
          ? 'EMERGENCY CLAMP'
          : `CLAMPING ${pressNum.toFixed(0)} kPa`

  const isEmergency = pressNum != null && pressNum > 3500
  const isActive = pressNum != null && pressNum >= 50

  return (
    <div
      className={`multi-meter-card tone-default brake-meter-card ${compact ? 'is-compact' : ''}`}
      data-testid={testId}
      aria-label="Hydraulic braking and mechanical caliper stroke meter"
    >
      {/* Card Header */}
      <div className="multi-meter-header">
        <div className="flex items-center gap-2 min-w-0">
          <span className="multi-meter-title">{title}</span>
          {compact && (
            <span className="multi-meter-subtitle">Host → RT → Actuator</span>
          )}
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

      {/* Layered Brake Pipeline Legend — Host Demand (0x301) → RT Setpoint (0x205) → Actual Pressure (0x721) */}
      <div className="steering-tier-legend brake-tier-legend" data-testid={`${testId}-tier-legend`}>
        <div className="tier-legend-item" title="Host Guidance Braking Demand (CAN 0x301)">
          <span className="tier-dot host" />
          <span>Host Demand</span>
        </div>
        <div className="tier-legend-item" title="RT Supervisory Setpoint (CAN 0x205)">
          <span className="tier-dot rt" />
          <span>RT Setpoint</span>
        </div>
        <div className="tier-legend-item" title="Actual Hydraulic Clamping Pressure (CAN 0x721)">
          <span className="tier-dot clamping" />
          <span>Actual Clamping</span>
        </div>
      </div>

      {/* Horizontal Clamping Pressure Section */}
      <div className="brake-horiz-section">
        {/* Large Digital Readout */}
        <div className="brake-horiz-readout-wrap">
          <div className="brake-clamp-status-tag">{clampingStateText}</div>
          <div className="flex items-baseline justify-center gap-2 my-1">
            <span
              className="brake-horiz-value"
              data-testid={`${testId}-center-value`}
            >
              {pressNum != null ? pressNum.toFixed(0) : '—'}
            </span>
            <span className="brake-horiz-unit">kPa</span>
          </div>
          <div
            className="brake-horiz-label"
            data-testid={`${testId}-center-label`}
            title="SEB_STATUS: pressure_kpa (CAN 0x721)"
          >
            <span>HYDRAULIC CLAMPING</span>
          </div>
        </div>

        {/* Primary Horizontal Pressure Meter Bar */}
        <div className="brake-horiz-meter-container">
          <div className="brake-horiz-meter-header">
            <span className="text-[10px] font-semibold text-muted uppercase tracking-wider">
              Hydraulic Circuit (0 - {maxPressure} kPa)
            </span>
            <span className="text-[11px] font-mono font-bold text-foreground">
              {pressNum != null ? `${pressNum.toFixed(0)} kPa` : '0 kPa'}
            </span>
          </div>

          <div className="brake-horiz-track-wrap">
            {/* The Bar Track */}
            <div className="brake-horiz-track">
              {/* Active Fill */}
              <div
                className={`brake-horiz-fill ${isEmergency ? 'fill-danger' : isActive ? 'fill-active' : 'fill-idle'}`}
                style={{ width: `${pressPct}%` }}
                title={`Actual Clamping Pressure: ${pressNum != null ? pressNum.toFixed(0) : 0} kPa (0x721)`}
              />

              {/* Host Demand Needle Marker */}
              {hostPressPct != null && (
                <div
                  className="brake-horiz-marker host-marker"
                  style={{ left: `${hostPressPct}%` }}
                  title={`Host Demand: ${hostPressNum?.toFixed(0)} kPa (0x301)`}
                />
              )}

              {/* RT Setpoint Needle Marker */}
              {rtPressPct != null && (
                <div
                  className="brake-horiz-marker rt-marker"
                  style={{ left: `${rtPressPct}%` }}
                  title={`RT Setpoint: ${rtPressNum?.toFixed(0)} kPa (0x205)`}
                />
              )}
            </div>

            {/* Scale Ticks & Labels */}
            <div className="brake-horiz-ticks-row">
              {pressureTicks.map((t) => (
                <div
                  key={`pt-${t.val}`}
                  className="brake-horiz-tick-item"
                  style={{ left: `${t.pct}%` }}
                >
                  <div className="brake-horiz-tick-line" />
                  <span className="brake-horiz-tick-label">{t.label}</span>
                </div>
              ))}
            </div>
          </div>
        </div>
      </div>

      {/* Secondary Horizontal Mechanical Caliper Travel Bar */}
      <div className="brake-stroke-container mt-3" data-testid={`${testId}-stroke-bar`}>
        <div className="brake-stroke-header">
          <div
            className="brake-stroke-label"
            title="SEB_STATUS: stroke_mm (CAN 0x721)"
          >
            <span>CALIPER TRAVEL</span>
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
            title={`Caliper stroke: ${strokeNum != null ? strokeNum.toFixed(1) : 0} mm (0x721)`}
          />
          {reqStrokePct != null && (
            <div
              className="brake-stroke-req-marker"
              style={{ left: `${reqStrokePct}%` }}
              title={`Requested Stroke: ${reqStrokeNum?.toFixed(1)} mm (0x7B9)`}
            />
          )}
        </div>
      </div>

      {/* Sub-meters Split Strip: Autonomy vs Supervisor */}
      <div className="multi-meter-sub-strip mt-3" data-testid={`${testId}-sub-strip`}>
        {/* Left Sub-Meter: Autonomy Command */}
        <div className="multi-meter-sub-col" data-testid={`${testId}-sub-left`}>
          <div
            className="multi-meter-sub-label-row"
            title="HOST_BRAKE_REQ: brake_pressure_kpa (CAN 0x301)"
          >
            <span className="multi-meter-sub-label">AUTONOMY DEMAND</span>
          </div>
          <span className="multi-meter-sub-value text-host">
            {typeof hostPressure === 'number' && Number.isFinite(hostPressure)
              ? hostPressure.toFixed(0)
              : '—'}
          </span>
          <span className="multi-meter-sub-unit">kPa Demand</span>
        </div>

        {/* Vertical Divider */}
        <div className="multi-meter-sub-divider" aria-hidden="true" />

        {/* Right Sub-Meter: Supervisor Command */}
        <div className="multi-meter-sub-col" data-testid={`${testId}-sub-right`}>
          <div
            className="multi-meter-sub-label-row"
            title="RT_BRAKE_CMD: brake_pressure_kpa (CAN 0x205)"
          >
            <span className="multi-meter-sub-label">SUPERVISOR SETPOINT</span>
          </div>
          <span className="multi-meter-sub-value text-rt">
            {typeof rtPressure === 'number' && Number.isFinite(rtPressure)
              ? rtPressure.toFixed(0)
              : '—'}
          </span>
          <span className="multi-meter-sub-unit">kPa Setpoint</span>
        </div>
      </div>

      {/* Secondary Hydraulics & Stroke Delta Row */}
      {!compact && (
        <div className="multi-meter-integrated-strip" data-testid={`${testId}-diag-strip`}>
          <div className="multi-meter-integrated-item" title="VCU_SEB_REQ: stroke_mm (CAN 0x7B9 via SYS)">
            <div className="integrated-label-row">
              <span className="integrated-label">Target</span>
            </div>
            <span className="integrated-value mono">
              {reqStrokeNum != null ? `${reqStrokeNum.toFixed(1)} mm` : '—'}
            </span>
          </div>

          <div className="multi-meter-integrated-item" title="Discrepancy between requested target and actual stroke">
            <div className="integrated-label-row">
              <span className="integrated-label">Delta</span>
            </div>
            <span className="integrated-value mono">
              {reqStrokeNum != null && strokeNum != null
                ? `${Math.abs(reqStrokeNum - strokeNum).toFixed(1)} mm`
                : '—'}
            </span>
          </div>

          <div className="multi-meter-integrated-item" title="Hydraulic Line Pressure in bar (1 bar = 100 kPa)">
            <div className="integrated-label-row">
              <span className="integrated-label">Pressure</span>
            </div>
            <span className="integrated-value mono">
              {pressNum != null ? `${(pressNum / 100).toFixed(1)} bar` : '—'}
            </span>
          </div>
        </div>
      )}
    </div>
  )
}

