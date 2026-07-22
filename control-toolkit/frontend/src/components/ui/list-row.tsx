import type { ButtonHTMLAttributes, HTMLAttributes, ReactNode } from 'react'
import { cn } from '../../lib/utils'
import { Button } from './button'

/** Dense engineering list row (Inject Active TX, monitor, jobs). */
export function ListRow({
  className,
  children,
  ...props
}: HTMLAttributes<HTMLDivElement>) {
  return (
    <div
      className={cn(
        'list-row grid grid-cols-[1fr_auto] items-center gap-1 border-b border-black/[0.05] py-1 last:border-b-0',
        className,
      )}
      {...props}
    >
      {children}
    </div>
  )
}

export function ListRowMain({
  className,
  ...props
}: ButtonHTMLAttributes<HTMLButtonElement>) {
  return (
    <button
      type="button"
      className={cn(
        'list-row-main grid min-h-0 min-w-0 max-h-none grid-cols-[auto_1fr] items-start gap-1.5',
        'rounded border-0 bg-transparent px-1 py-0.5 text-left text-[11px] font-medium text-text shadow-none',
        'hover:bg-surface-2 focus:bg-surface-2 active:bg-surface-2',
        className,
      )}
      {...props}
    />
  )
}

export function ListRowMeta({
  className,
  children,
}: {
  className?: string
  children: ReactNode
}) {
  return (
    <span className={cn('flex min-w-0 flex-col gap-px', className)}>{children}</span>
  )
}

export function ListRowStop({
  className,
  ...props
}: ButtonHTMLAttributes<HTMLButtonElement>) {
  return (
    <Button
      variant="secondary"
      size="sm"
      className={cn('danger-text h-[26px] min-h-[26px] max-h-[26px] px-1.5 text-[11px]', className)}
      {...props}
    />
  )
}

export function BusChip({
  bus,
  className,
}: {
  bus: string
  className?: string
}) {
  const low = bus.toLowerCase() === 'low'
  return (
    <span
      className={cn(
        'inline-grid h-[18px] w-[18px] place-items-center rounded-[3px] font-mono text-[10px] font-bold',
        low ? 'bg-warning-soft text-warning' : 'bg-info-soft text-info',
        className,
      )}
    >
      {low ? 'L' : 'H'}
    </span>
  )
}
