import { cn } from '../../lib/utils'

export type StatusDotTone =
  | 'live'
  | 'success'
  | 'tx'
  | 'warning'
  | 'danger'
  | 'muted'

const TONE_CLASS: Record<StatusDotTone, string> = {
  live: 'live',
  success: 'success',
  /** Host-side CAN output we drive (inject / control) — blue, not ECU RX green. */
  tx: 'tx',
  warning: 'warning',
  danger: 'danger',
  muted: 'muted',
}

/** Binary / state lamp — reuse everywhere (monitor, ECU, TX). */
export function StatusDot({
  tone = 'muted',
  className,
  title,
}: {
  tone?: StatusDotTone
  className?: string
  title?: string
}) {
  return (
    <span
      className={cn('status-dot', TONE_CLASS[tone], className)}
      title={title}
      aria-hidden={title ? undefined : true}
      role={title ? 'img' : undefined}
      aria-label={title}
    />
  )
}

/** Map live/late → live; else dead. */
export function toneFromLive(live: boolean): StatusDotTone {
  return live ? 'live' : 'danger'
}
