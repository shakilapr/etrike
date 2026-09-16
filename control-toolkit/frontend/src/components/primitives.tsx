import { cn } from '../lib/utils'
import { FreshnessBadge } from './FreshnessBadge'
import { Badge } from './ui/badge'

export function LivenessBadge({ value }: { value: string }) {
  const key = value.toLowerCase()
  const tone =
    key === 'live'
      ? 'ok'
      : key === 'late'
        ? 'warn'
        : key === 'fault'
          ? 'danger'
          : key === 'offline' || key === 'missing'
            ? 'muted'
            : 'muted'
  return (
    <Badge tone={tone} withDot={false} className={`live-badge live-${key}`}>
      {value}
    </Badge>
  )
}

/**
 * Progress bar only for continuous engineering quantities (pressure, speed, angle…).
 * Do not use for binary flags, enums, or few-state status — use StatusPill instead.
 */
export function MeterBar({
  value,
  max,
  min = 0,
  tone,
  label,
  testId,
  bipolar,
}: {
  value: number | null
  max: number
  min?: number
  tone?: 'auto' | 'high-bad' | 'low-bad' | 'accent' | 'ok' | 'warn' | 'danger'
  label?: string
  testId?: string
  bipolar?: boolean
}) {
  const isBipolar = bipolar ?? (min < 0 && max > 0)
  const span = Math.max(1e-6, max - min)
  
  let fillStyle: React.CSSProperties
  let pct = 0

  if (isBipolar) {
    // For steering and yaw: 0 is center. Bar extends left or right from 50%.
    const halfSpan = Math.max(Math.abs(min), Math.abs(max))
    const clampedVal = value == null ? 0 : Math.max(-halfSpan, Math.min(halfSpan, value))
    pct = (Math.abs(clampedVal) / halfSpan) * 50 // max 50% width
    if (clampedVal < 0) {
      fillStyle = {
        width: `${pct}%`,
        right: '50%',
        position: 'absolute',
      }
    } else {
      fillStyle = {
        width: `${pct}%`,
        left: '50%',
        position: 'absolute',
      }
    }
  } else {
    const raw = value == null ? 0 : Math.abs(value - min) / span
    pct = Math.max(0, Math.min(100, raw * 100))
    fillStyle = { width: `${pct}%` }
  }

  let t = tone ?? 'auto'
  if (t === 'auto' || t === 'high-bad' || t === 'low-bad') {
    if (t === 'high-bad') {
      t = pct >= 70 ? 'danger' : pct >= 40 ? 'warn' : 'ok'
    } else if (t === 'low-bad') {
      t = pct <= 15 ? 'danger' : pct <= 35 ? 'warn' : 'ok'
    } else {
      t = pct >= 90 ? 'warn' : 'accent'
    }
  }
  const fill =
    t === 'danger'
      ? 'bg-danger'
      : t === 'warn'
        ? 'bg-warning'
        : t === 'ok'
          ? 'bg-success'
          : 'bg-primary'

  return (
    <div
      className={cn('meter-bar', `tone-${t}`, 'w-full')}
      data-testid={testId}
      title={label}
      role="meter"
      aria-valuemin={min}
      aria-valuemax={max}
      aria-valuenow={value ?? undefined}
      aria-label={label}
    >
      <div className="meter-bar-track relative h-1.5 w-full overflow-hidden rounded-full bg-surface-2">
        {isBipolar && (
          <div className="absolute left-1/2 top-0 bottom-0 w-[1px] -translate-x-1/2 bg-border z-10 opacity-75" />
        )}
        <div
          className={cn('meter-bar-fill h-full rounded-full transition-[width,left,right] duration-150', fill)}
          style={fillStyle}
        />
      </div>
    </div>
  )
}


/** Discrete state (binary / enum / few values) — never a progress bar. */
export function StatusPill({
  label,
  tone = 'muted',
  testId,
}: {
  label: string
  tone?: 'ok' | 'warn' | 'danger' | 'muted' | 'accent'
  testId?: string
}) {
  return (
    <Badge
      tone={tone}
      withDot={false}
      className={cn('status-pill', `tone-${tone}`)}
      data-testid={testId}
    >
      {label}
    </Badge>
  )
}

export function MetricCard({
  title,
  valueText,
  unit,
  sub,
  freshness,
  value,
  max,
  min,
  tone,
  testId,
  meterTestId,
  showMeter = true,
  badges,
  bipolar,
}: {
  title: string
  valueText: string
  unit?: string
  sub?: string
  freshness?: string
  value?: number | null
  max?: number
  min?: number
  tone?: 'auto' | 'high-bad' | 'low-bad' | 'accent' | 'ok' | 'warn' | 'danger'
  testId?: string
  meterTestId?: string
  showMeter?: boolean
  badges?: React.ReactNode
  bipolar?: boolean
}) {
  return (
    <div
      className="card metric-card rounded-[var(--radius)] border border-border bg-surface p-2.5"
      data-testid={testId}
    >
      <div className="card-head mb-0.5 flex items-start justify-between gap-1.5">
        <div className="card-title text-[11px] font-semibold text-text-secondary">{title}</div>
        {freshness ? <FreshnessBadge value={freshness} /> : null}
      </div>
      <div
        className="metric text-[22px] font-semibold tabular-nums tracking-tight text-text"
        data-testid={
          testId === 'card-speed'
            ? 'metric-speed'
            : testId === 'card-yaw'
              ? 'metric-yaw'
              : testId === 'card-gear'
                ? 'metric-gear'
                : testId
                  ? `${testId}-value`
                  : undefined
        }
      >
        {valueText}
        {unit ? <span className="unit ml-1 text-xs font-medium text-text-secondary"> {unit}</span> : null}
      </div>
      {showMeter && max != null ? (
        <MeterBar
          value={value ?? null}
          max={max}
          min={min}
          tone={tone}
          label={title}
          testId={meterTestId}
          bipolar={bipolar}
        />
      ) : null}
      {badges ? <div className="card-micro-badges">{badges}</div> : null}
      {sub ? <div className="card-sub muted mt-1 text-xs text-text-secondary">{sub}</div> : null}
    </div>
  )
}

