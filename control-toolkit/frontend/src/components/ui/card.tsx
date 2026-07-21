import type { HTMLAttributes, ReactNode } from 'react'
import { cn } from '../../lib/utils'

/** Panel / card surface used by workspaces (replaces bare `.panel` over time). */
export function Card({
  className,
  title,
  children,
  testId,
  ...props
}: HTMLAttributes<HTMLElement> & {
  title?: ReactNode
  testId?: string
}) {
  return (
    <section
      className={cn(
        'panel overflow-hidden rounded-[var(--radius)] border border-border bg-surface p-4',
        className,
      )}
      data-testid={testId}
      {...props}
    >
      {title != null && title !== '' ? (
        <h2 className="mb-2.5 mt-0 text-[13px] font-bold text-text">{title}</h2>
      ) : null}
      {children}
    </section>
  )
}

export function CardHead({ className, ...props }: HTMLAttributes<HTMLDivElement>) {
  return (
    <div className={cn('card-head mb-1 flex items-start justify-between gap-2', className)} {...props} />
  )
}

export function CardTitle({ className, ...props }: HTMLAttributes<HTMLDivElement>) {
  return <div className={cn('card-title text-[12px] font-semibold text-text-secondary', className)} {...props} />
}
