import { cva, type VariantProps } from 'class-variance-authority'
import type { HTMLAttributes } from 'react'
import { cn } from '../../lib/utils'

const badgeVariants = cva(
  'inline-flex items-center gap-1.5 rounded-full border px-2 py-0.5 text-[11px] font-semibold capitalize',
  {
    variants: {
      tone: {
        muted: 'border-border bg-surface-subtle text-text-secondary',
        ok: 'border-[#c5e4d2] bg-[var(--success-soft,#eaf6f0)] text-success',
        warn: 'border-[#f0d48a] bg-[var(--warning-soft,#fff7e0)] text-warning',
        danger: 'border-[#e4aeb2] bg-[var(--danger-soft,#fff0f1)] text-danger',
        accent: 'border-[#c5d5ec] bg-primary-soft text-primary',
        info: 'border-[#c5d5ec] bg-[var(--info-soft,#edf3fb)] text-[var(--info,#315d9c)]',
      },
      withDot: {
        true: 'before:h-1.5 before:w-1.5 before:shrink-0 before:rounded-full before:bg-current before:content-[""]',
        false: '',
      },
    },
    defaultVariants: {
      tone: 'muted',
      withDot: false,
    },
  },
)

export type BadgeProps = HTMLAttributes<HTMLSpanElement> &
  VariantProps<typeof badgeVariants>

export function Badge({ className, tone, withDot, ...props }: BadgeProps) {
  return <span className={cn(badgeVariants({ tone, withDot }), className)} {...props} />
}

export { badgeVariants }
