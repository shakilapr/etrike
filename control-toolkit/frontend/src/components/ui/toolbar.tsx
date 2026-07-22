import type { HTMLAttributes, ReactNode } from 'react'
import { cn } from '../../lib/utils'

/** Dense single-row workspace toolbar (Inject, etc.). */
export function Toolbar({
  className,
  children,
  ...props
}: HTMLAttributes<HTMLDivElement>) {
  return (
    <div
      className={cn(
        'toolbar-bar mb-3 flex flex-wrap items-center justify-between gap-2 rounded-[var(--radius)]',
        'border border-border bg-surface px-3 py-2',
        className,
      )}
      {...props}
    >
      {children}
    </div>
  )
}

export function ToolbarGroup({
  className,
  children,
}: {
  className?: string
  children: ReactNode
}) {
  return (
    <div className={cn('flex min-w-0 flex-wrap items-center gap-2', className)}>{children}</div>
  )
}

export function ToolbarItem({
  label,
  children,
  className,
}: {
  label?: string
  children: ReactNode
  className?: string
}) {
  return (
    <span className={cn('inline-flex items-baseline gap-1.5 text-xs', className)}>
      {label != null && (
        <span className="text-[10.5px] font-semibold uppercase tracking-wide text-text-tertiary">
          {label}
        </span>
      )}
      {children}
    </span>
  )
}

export function ToolbarDivider({ className }: { className?: string }) {
  return <span className={cn('h-[22px] w-px bg-border', className)} aria-hidden />
}
