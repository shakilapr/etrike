import { cva, type VariantProps } from 'class-variance-authority'
import type { ButtonHTMLAttributes } from 'react'
import { cn } from '../../lib/utils'

/**
 * Action button — variants map to App.css skins (`.secondary`, `.danger`, `.dense`)
 * so geometry locks and e2e selectors stay stable during migration.
 */
const buttonVariants = cva(
  [
    'inline-flex items-center justify-center gap-1.5',
    'rounded-[var(--radius)] text-[13px] font-semibold leading-tight',
    'transition-[background,border-color,color] duration-150',
    'focus-visible:outline focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-[#9bb9e8]',
    'disabled:cursor-not-allowed disabled:opacity-45',
    'transform-none',
  ].join(' '),
  {
    variants: {
      variant: {
        /** Primary filled — also carries `.btn` for App.css parity */
        default: 'btn border border-primary bg-primary text-white hover:border-primary-hover hover:bg-primary-hover',
        secondary:
          'secondary border border-border-strong bg-surface text-text hover:bg-surface-subtle',
        danger: 'danger border border-danger bg-danger text-white hover:opacity-90',
        ghost: 'border border-transparent bg-transparent text-text-secondary hover:bg-surface-subtle hover:text-text',
      },
      size: {
        default: 'h-9 min-h-9 max-h-9 px-3.5',
        dense: 'dense h-8 min-h-8 max-h-8 px-3 text-[12.5px]',
        sm: 'h-8 min-h-8 max-h-8 px-2.5 text-xs',
      },
    },
    defaultVariants: {
      variant: 'default',
      size: 'default',
    },
  },
)

export type ButtonProps = ButtonHTMLAttributes<HTMLButtonElement> &
  VariantProps<typeof buttonVariants>

export function Button({ className, variant, size, type = 'button', ...props }: ButtonProps) {
  return (
    <button type={type} className={cn(buttonVariants({ variant, size }), className)} {...props} />
  )
}

export { buttonVariants }
