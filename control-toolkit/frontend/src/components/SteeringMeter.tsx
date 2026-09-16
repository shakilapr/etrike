import { useMemo } from 'react'

export type SteeringMeterProps = {
  title?: string
  badge?: string
  badgeTone?: 'ok' | 'warn' | 'danger' | 'info' | 'muted'
  actualAngle: number | null | undefined
  hostAngle: number | null | undefined
  rtAngle: number | null | undefined
  yawRate?: number | null | undefined
  slewRate?: number | null | undefined
  torqueNm?: number | null | undefined
  maxAngle?: number
  compact?: boolean
  testId?: string
  protocolAudit?: {
    status: 'conforming' | 'warning' | 'error' | 'unverified'
    note?: string
  }
}

// Helpers defined outside component for stable reference
function makeBaselinePath(cx: number, cy: number, r: number) {
  return `M ${cx - r} ${cy} A ${r} ${r} 0 0 1 ${cx + r} ${cy}`
}

function makeSweepArc(cx: number, cy: number, r: number, ratio: number | null) {
  if (ratio == null || Math.abs(ratio) < 0.005) return null
  const deg = -90 + ratio * 90
  const rad = (deg * Math.PI) / 180
  const x0 = cx
  const y0 = cy - r
  const x1 = cx + r * Math.cos(rad)
  const y1 = cy + r * Math.sin(rad)
  const sweep = ratio > 0 ? 1 : 0
  return `M ${x0} ${y0} A ${r} ${r} 0 0 ${sweep} ${x1} ${y1}`
}

function toVisualDeg(ratio: number) {
  return -90 + ratio * 90
}

export function SteeringMeter({
  title = 'Steering',
  badge = 'EPS Closed-Loop',
  badgeTone = 'info',
  actualAngle,
  hostAngle,
  rtAngle,
  yawRate,
  slewRate,
  torqueNm,
  maxAngle = 90,
  compact = false,
  testId = 'meter-steer',
  protocolAudit,
}: SteeringMeterProps) {
  // Center & Radii:
  // Control Pipeline Order from Inside-Out:
  // Layer 1 (Innermost): Host Guidance Target (0x303, Electric Blue)
  // Layer 2 (Middle): RT Setpoint (0x169, Emerald Green)
  // Layer 3 (Outermost/Top): SES Actual Feedback (0x201, Black/Charcoal)
  const CX = 175
  const CY = compact ? 138 : 158
  const R_ACTUAL = compact ? 104 : 124 // Outer
  const R_RT = compact ? 90 : 110      // Middle
  const R_HOST = compact ? 76 : 96     // Inner

  // 1. Raw numerical values — NEVER clamped for display so values can go beyond +/-90°!
  const angleNum =
    typeof actualAngle === 'number' && Number.isFinite(actualAngle)
      ? actualAngle
      : null

  const hostNum =
    typeof hostAngle === 'number' && Number.isFinite(hostAngle)
      ? hostAngle
      : null

  const rtNum =
    typeof rtAngle === 'number' && Number.isFinite(rtAngle)
      ? rtAngle
      : null

  // 2. Clamped ratios for visual gauge rendering [-1, 1]
  // 0° is 12 o'clock (Cartesian -90°). -90° is Left (-180°), +90° is Right (0°).
  const actualRatio = useMemo(() => {
    if (angleNum == null) return null
    return Math.max(-1, Math.min(1, angleNum / maxAngle))
  }, [angleNum, maxAngle])

  const hostRatio = useMemo(() => {
    if (hostNum == null) return null
    return Math.max(-1, Math.min(1, hostNum / maxAngle))
  }, [hostNum, maxAngle])

  const rtRatio = useMemo(() => {
    if (rtNum == null) return null
    return Math.max(-1, Math.min(1, rtNum / maxAngle))
  }, [rtNum, maxAngle])

  // 3. Baseline 180° track paths for all 3 layers
  const actualBaseline = useMemo(() => makeBaselinePath(CX, CY, R_ACTUAL), [R_ACTUAL, CY])
  const hostBaseline = useMemo(() => makeBaselinePath(CX, CY, R_HOST), [R_HOST, CY])
  const rtBaseline = useMemo(() => makeBaselinePath(CX, CY, R_RT), [R_RT, CY])

  // 4. Active Sweep Arcs from center (0° top) to current angle
  const actualSweepArc = useMemo(() => makeSweepArc(CX, CY, R_ACTUAL, actualRatio), [R_ACTUAL, actualRatio, CY])
  const hostSweepArc = useMemo(() => makeSweepArc(CX, CY, R_HOST, hostRatio), [R_HOST, hostRatio, CY])
  const rtSweepArc = useMemo(() => makeSweepArc(CX, CY, R_RT, rtRatio), [R_RT, rtRatio, CY])

  // 5. Pointer Cursors / Needle lines at each current angle
  // Outer Actual cursor needle: extends across R_ACTUAL
  const actualCursor = useMemo(() => {
    if (actualRatio == null) return null
    const rad = (toVisualDeg(actualRatio) * Math.PI) / 180
    return {
      x1: CX + (R_ACTUAL - 7) * Math.cos(rad),
      y1: CY + (R_ACTUAL - 7) * Math.sin(rad),
      x2: CX + (R_ACTUAL + 7) * Math.cos(rad),
      y2: CY + (R_ACTUAL + 7) * Math.sin(rad),
    }
  }, [actualRatio, R_ACTUAL, CY])

  // Middle RT cursor line: extends across R_RT
  const rtCursor = useMemo(() => {
    if (rtRatio == null) return null
    const rad = (toVisualDeg(rtRatio) * Math.PI) / 180
    return {
      x1: CX + (R_RT - 6) * Math.cos(rad),
      y1: CY + (R_RT - 6) * Math.sin(rad),
      x2: CX + (R_RT + 5) * Math.cos(rad),
      y2: CY + (R_RT + 5) * Math.sin(rad),
    }
  }, [rtRatio, R_RT, CY])

  // Inner Host cursor line: extends across R_HOST
  const hostCursor = useMemo(() => {
    if (hostRatio == null) return null
    const rad = (toVisualDeg(hostRatio) * Math.PI) / 180
    return {
      x1: CX + (R_HOST - 6) * Math.cos(rad),
      y1: CY + (R_HOST - 6) * Math.sin(rad),
      x2: CX + (R_HOST + 5) * Math.cos(rad),
      y2: CY + (R_HOST + 5) * Math.sin(rad),
    }
  }, [hostRatio, R_HOST, CY])

  // 6. Scale Ticks & Labels: -90° to +90°
  const ticks = useMemo(() => {
    const list: Array<{ val: number; label: string; x1: number; y1: number; x2: number; y2: number; lx: number; ly: number }> = []
    const stepList = compact ? [-90, -45, 0, 45, 90] : [-90, -60, -30, 0, 30, 60, 90]

    for (const val of stepList) {
      const r = val / maxAngle
      const deg = toVisualDeg(r)
      const rad = (deg * Math.PI) / 180

      const tickLen = val === 0 ? (compact ? 9 : 12) : (compact ? 6 : 8)
      const innerR = R_ACTUAL
      const x1 = CX + (innerR - tickLen) * Math.cos(rad)
      const y1 = CY + (innerR - tickLen) * Math.sin(rad)
      const x2 = CX + (innerR + 3) * Math.cos(rad)
      const y2 = CY + (innerR + 3) * Math.sin(rad)

      const labelR = R_ACTUAL + (compact ? 13 : 18)
      const lx = CX + labelR * Math.cos(rad)
      const ly = CY + labelR * Math.sin(rad)

      list.push({
        val,
        label: val === 0 ? '0°' : `${val > 0 ? '+' : ''}${val}°`,
        x1,
        y1,
        x2,
        y2,
        lx,
        ly,
      })
    }
    return list
  }, [maxAngle, R_ACTUAL, CY, compact])

  // Minor ticks
  const minorTicks = useMemo(() => {
    if (compact) return []
    const list: Array<{ x1: number; y1: number; x2: number; y2: number }> = []
    const steps = [-75, -45, -15, 15, 45, 75]
    for (const val of steps) {
      const r = val / maxAngle
      const deg = toVisualDeg(r)
      const rad = (deg * Math.PI) / 180
      const innerR = R_ACTUAL
      const tickLen = 5
      list.push({
        x1: CX + (innerR - tickLen) * Math.cos(rad),
        y1: CY + (innerR - tickLen) * Math.sin(rad),
        x2: CX + innerR * Math.cos(rad),
        y2: CY + innerR * Math.sin(rad),
      })
    }
    return list
  }, [maxAngle, R_ACTUAL, CY, compact])

  // Direction badge & overflow check
  const isOverflow = angleNum != null && Math.abs(angleNum) > maxAngle
  const directionText =
    angleNum == null
      ? '—'
      : Math.abs(angleNum) < 0.5
        ? 'CENTER'
        : angleNum < 0
          ? `◀ LEFT ${Math.abs(angleNum).toFixed(1)}°`
          : `RIGHT ▶ ${angleNum.toFixed(1)}°`

  return (
    <div
      className={`multi-meter-card tone-default steering-meter-card ${compact ? 'is-compact' : ''}`}
      data-testid={testId}
      aria-label="Steering angle direction meter"
    >
      {/* Card Header */}
      <div className="multi-meter-header">
        <div className="flex items-center gap-2">
          <span className="multi-meter-title">{title}</span>
          <span className="multi-meter-subtitle">
            {compact ? 'Host → RT → SES' : 'Directional Angle & Multi-Tier Guidance'}
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

      {/* Layered Meter Lines Legend — in physical order: Host (inner) → RT (middle) → SES (outer) */}
      <div className="steering-tier-legend" data-testid={`${testId}-tier-legend`}>
        <div className="tier-legend-item">
          <span className="tier-dot host" />
          <span>Host Target</span>
          <span className="can-id-tag">0x303</span>
        </div>
        <div className="tier-legend-item">
          <span className="tier-dot rt" />
          <span>RT Setpoint</span>
          <span className="can-id-tag">0x169</span>
        </div>
        <div className="tier-legend-item">
          <span className="tier-dot actual" />
          <span>Actual SES</span>
          <span className="can-id-tag">0x201</span>
        </div>
      </div>

      {/* Steering Dial SVG with 3 Concentric Layered Tracks */}
      <div className="multi-meter-gauge-wrap steering-gauge-wrap">
        <svg
          viewBox={compact ? "0 0 350 172" : "0 0 350 215"}
          className="multi-meter-svg steering-svg"
          preserveAspectRatio="xMidYMid meet"
        >
          {/* Layer 1 (Innermost): Host Guidance Target (R_HOST, Electric Blue) */}
          <path d={hostBaseline} fill="none" className="steering-track-host" />
          {hostSweepArc && <path d={hostSweepArc} fill="none" className="steering-arc-host" />}
          {hostCursor && (
            <g>
              <title>{`Host Target Steer: ${hostNum != null ? `${hostNum > 0 ? '+' : ''}${hostNum.toFixed(1)}°` : '—'} (0x303)`}</title>
              <line
                x1={hostCursor.x1}
                y1={hostCursor.y1}
                x2={hostCursor.x2}
                y2={hostCursor.y2}
                className="steering-cursor-host"
              />
            </g>
          )}

          {/* Layer 2 (Middle): RT Setpoint (R_RT, Emerald Green) */}
          <path d={rtBaseline} fill="none" className="steering-track-rt" />
          {rtSweepArc && <path d={rtSweepArc} fill="none" className="steering-arc-rt" />}
          {rtCursor && (
            <g>
              <title>{`RT Target Steer: ${rtNum != null ? `${rtNum > 0 ? '+' : ''}${rtNum.toFixed(1)}°` : '—'} (0x169)`}</title>
              <line
                x1={rtCursor.x1}
                y1={rtCursor.y1}
                x2={rtCursor.x2}
                y2={rtCursor.y2}
                className="steering-cursor-rt"
              />
            </g>
          )}

          {/* Layer 3 (Outermost/Top): Actual Steering Angle (R_ACTUAL, High-Contrast Black/Charcoal) */}
          <path d={actualBaseline} fill="none" className="steering-track-actual" />
          {actualSweepArc && <path d={actualSweepArc} fill="none" className="steering-arc-actual" />}
          {actualCursor && (
            <g>
              <title>{`Actual Steering Angle: ${angleNum != null ? `${angleNum > 0 ? '+' : ''}${angleNum.toFixed(1)}°` : '—'} (0x201)`}</title>
              <line
                x1={actualCursor.x1}
                y1={actualCursor.y1}
                x2={actualCursor.x2}
                y2={actualCursor.y2}
                className="steering-cursor-actual"
              />
            </g>
          )}

          {/* Minor Ticks */}
          {minorTicks.map((mt, idx) => (
            <line
              key={`sm-${idx}`}
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
              key={`st-${t.val}`}
              x1={t.x1}
              y1={t.y1}
              x2={t.x2}
              y2={t.y2}
              className={t.val === 0 ? 'steering-tick-zero' : 'multi-meter-tick-major'}
            />
          ))}

          {/* Scale Labels */}
          {ticks.map((t) => (
            <text
              key={`stl-${t.val}`}
              x={t.lx}
              y={t.ly}
              className="multi-meter-tick-text"
              textAnchor="middle"
              dominantBaseline="middle"
            >
              {t.label}
            </text>
          ))}
        </svg>

        {/* Center Digital Readout — safely inside R_HOST with ZERO hub/needle overlap */}
        <div className="steering-center-readout">
          <div className="flex items-center gap-1.5">
            <span className="steering-direction-badge">{directionText}</span>
            {isOverflow && (
              <span
                className="steering-overflow-badge"
                title={`Steering angle ${angleNum?.toFixed(1)}° exceeds visual ±90° dial limit`}
              >
                {angleNum! > maxAngle ? '> +90°' : '< -90°'}
              </span>
            )}
          </div>
          <div className="multi-meter-center-value" data-testid={`${testId}-center-value`}>
            {angleNum != null ? `${angleNum > 0 ? '+' : ''}${angleNum.toFixed(1)}°` : '—'}
          </div>
          <div className="multi-meter-center-label" data-testid={`${testId}-center-label`}>
            <span>SES ANGLE</span>
            <span
              className="can-id-tag"
              title="SES_STATUS: angle_deg (Low 0x201)"
              data-testid={`${testId}-primary-can`}
            >
              0x201
            </span>
          </div>
        </div>
      </div>

      {/* Sub-meters Split Strip (Host vs RT Command) */}
      <div className="multi-meter-sub-strip" data-testid={`${testId}-sub-strip`}>
        {/* Left Sub-Meter: Host Command */}
        <div className="multi-meter-sub-col" data-testid={`${testId}-sub-left`}>
          <div className="multi-meter-sub-label-row">
            <span className="tier-dot host" />
            <span className="multi-meter-sub-label">HOST STEER</span>
            <span
              className="can-id-tag"
              title="HOST_STEER_CMD: steer_angle_0_1deg (High 0x303)"
              data-testid={`${testId}-sub-left-can`}
            >
              0x303
            </span>
          </div>
          <span className="multi-meter-sub-value text-host">
            {hostNum != null
              ? `${hostNum > 0 ? '+' : ''}${hostNum.toFixed(1)}°`
              : '—'}
          </span>
          <span className="multi-meter-sub-unit">Planner Target</span>
        </div>

        {/* Vertical Divider */}
        <div className="multi-meter-sub-divider" aria-hidden="true" />

        {/* Right Sub-Meter: RT Command */}
        <div className="multi-meter-sub-col" data-testid={`${testId}-sub-right`}>
          <div className="multi-meter-sub-label-row">
            <span className="tier-dot rt" />
            <span className="multi-meter-sub-label">RT TARGET</span>
            <span
              className="can-id-tag"
              title="VCU_SES_REQ: target_angle_raw (Low 0x169)"
              data-testid={`${testId}-sub-right-can`}
            >
              0x169
            </span>
          </div>
          <span className="multi-meter-sub-value text-rt">
            {rtNum != null
              ? `${rtNum > 0 ? '+' : ''}${rtNum.toFixed(1)}°`
              : '—'}
          </span>
          <span className="multi-meter-sub-unit">EPS Setpoint</span>
        </div>
      </div>

      {/* Combined Integrated Steering Dynamics (Yaw, Slew, Torque) */}
      <div className="multi-meter-integrated-strip" data-testid={`${testId}-dynamics-strip`}>
        <div className="multi-meter-integrated-item" title="HOST_DRIVE_CMD: yaw_rate_mrad_s (High 0x300)">
          <div className="integrated-label-row">
            <span className="integrated-label">Host Yaw</span>
            <span className="can-id-tag" title="HOST_DRIVE_CMD: yaw_rate_mrad_s">
              0x300
            </span>
          </div>
          <span className="integrated-value mono">
            {typeof yawRate === 'number' && Number.isFinite(yawRate)
              ? `${yawRate.toFixed(0)} mrad/s`
              : '—'}
          </span>
        </div>

        <div className="multi-meter-integrated-item" title="VCU_SES_REQ: target_speed_raw (Low 0x169)">
          <div className="integrated-label-row">
            <span className="integrated-label">RT Slew</span>
            <span className="can-id-tag" title="VCU_SES_REQ: target_speed_raw">
              0x169
            </span>
          </div>
          <span className="integrated-value mono">
            {typeof slewRate === 'number' && Number.isFinite(slewRate)
              ? `${slewRate.toFixed(0)}°/s`
              : '—'}
          </span>
        </div>

        <div className="multi-meter-integrated-item" title="SES_STATUS: torque_nm (Low 0x201)">
          <div className="integrated-label-row">
            <span className="integrated-label">EPS Torque</span>
            <span className="can-id-tag" title="SES_STATUS: torque_nm">
              0x201
            </span>
          </div>
          <span className="integrated-value mono">
            {typeof torqueNm === 'number' && Number.isFinite(torqueNm)
              ? `${torqueNm.toFixed(1)} Nm`
              : '—'}
          </span>
        </div>
      </div>
    </div>
  )
}
