import { Badge } from './ui/badge'

const TONE: Record<string, 'ok' | 'warn' | 'danger' | 'muted' | 'info'> = {
  live: 'ok',
  late: 'warn',
  missing: 'muted',
  unseen: 'muted',
  invalid: 'danger',
  frozen: 'info',
  recovering: 'info',
}

/** Freshness pill — Tailwind Badge + legacy `.fresh` class for any remaining CSS hooks. */
export function FreshnessBadge({ value }: { value: string }) {
  const key = value.toLowerCase()
  return (
    <Badge
      tone={TONE[key] ?? 'muted'}
      withDot={false}
      className={`fresh fresh-${key}`}
      data-testid="freshness"
    >
      {value}
    </Badge>
  )
}
