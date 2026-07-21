import type { ButtonHTMLAttributes, HTMLAttributes, ReactNode } from 'react'
import { cn } from '../../lib/utils'

/** Segmented control group (bus filter, view mode, profile sub-select). */
export function Seg({ className, ...props }: HTMLAttributes<HTMLDivElement>) {
  return (
    <div
      className={cn(
        'seg inline-flex overflow-hidden rounded-[var(--radius)] border border-border bg-surface',
        className,
      )}
      {...props}
    />
  )
}

export function SegButton({
  className,
  active,
  children,
  ...props
}: ButtonHTMLAttributes<HTMLButtonElement> & {
  active?: boolean
  children: ReactNode
}) {
  return (
    <button
      type="button"
      className={cn('seg-btn', active && 'active', className)}
      {...props}
    >
      {children}
    </button>
  )
}
