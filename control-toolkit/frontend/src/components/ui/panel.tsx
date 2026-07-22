import type { HTMLAttributes, ReactNode } from 'react'
import { cn } from '../../lib/utils'

/** Standard content panel (replaces ad-hoc `.panel` over time). */
export function Panel({
  className,
  children,
  ...props
}: HTMLAttributes<HTMLElement>) {
  return (
    <section
      className={cn(
        'panel rounded-[var(--radius)] border border-border bg-surface p-3.5',
        className,
      )}
      {...props}
    >
      {children}
    </section>
  )
}

export function PanelTitle({
  className,
  children,
  trailing,
}: {
  className?: string
  children: ReactNode
  trailing?: ReactNode
}) {
  return (
    <div className={cn('mb-2 flex items-center justify-between gap-2', className)}>
      <h2 className="m-0 text-[13px] font-bold text-text">{children}</h2>
      {trailing}
    </div>
  )
}
