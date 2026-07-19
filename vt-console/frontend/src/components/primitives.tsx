import clsx from 'clsx'
import type { ReactNode } from 'react'

export function Card({ title, children, className }: { title?: ReactNode; children: ReactNode; className?: string }) {
  return (
    <div className={clsx('rounded-md border border-border bg-surface p-4', className)}>
      {title && <div className="mb-2 text-xs font-medium uppercase tracking-wide text-text-dim">{title}</div>}
      {children}
    </div>
  )
}

export function Badge({ children, className }: { children: ReactNode; className?: string }) {
  return (
    <span
      className={clsx(
        'inline-flex items-center rounded px-1.5 py-0.5 text-xs font-medium leading-none',
        className,
      )}
    >
      {children}
    </span>
  )
}

// A dot + label pill, used for freshness/health/liveness everywhere so the
// same status always looks the same (architecture §17).
export function StatusPill({ label, colorClass, dotClass }: { label: string; colorClass: string; dotClass?: string }) {
  return (
    <span className={clsx('inline-flex items-center gap-1.5 rounded px-2 py-0.5 text-xs font-medium', colorClass)}>
      <span className={clsx('h-1.5 w-1.5 rounded-full', dotClass ?? 'bg-current')} />
      {label}
    </span>
  )
}

export function BusBadge({ bus }: { bus: 'high' | 'low' }) {
  return (
    <Badge className={bus === 'high' ? 'bg-bus-high/15 text-bus-high' : 'bg-bus-low/15 text-bus-low'}>
      {bus === 'high' ? 'High' : 'Low'}
    </Badge>
  )
}

export function Unknown() {
  return <span className="text-text-faint">Unknown</span>
}
