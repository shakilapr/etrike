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
}: {
  value: number | null
  max: number
  min?: number
  tone?: 'auto' | 'high-bad' | 'low-bad' | 'accent' | 'ok' | 'warn' | 'danger'
  label?: string
  testId?: string
}) {
  const span = Math.max(1e-6, max - min)
  const raw = value == null ? 0 : Math.abs(value - min) / span
  const pct = Math.max(0, Math.min(100, raw * 100))
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
      <div className="meter-bar-track h-1.5 w-full overflow-hidden rounded-full bg-surface-2">
        <div
          className={cn('meter-bar-fill h-full rounded-full transition-[width] duration-150', fill)}
          style={{ width: `${pct}%` }}
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
}) {
  return (
    <div
      className="card metric-card rounded-[var(--radius)] border border-border bg-surface p-3.5"
      data-testid={testId}
    >
      <div className="card-head mb-1 flex items-start justify-between gap-2">
        <div className="card-title text-[12px] font-semibold text-text-secondary">{title}</div>
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
        />
      ) : null}
      {sub ? <div className="card-sub muted mt-1 text-xs text-text-secondary">{sub}</div> : null}
    </div>
  )
}
