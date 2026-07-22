import { cva, type VariantProps } from 'class-variance-authority'
import type { ButtonHTMLAttributes } from 'react'
import { cn } from '../../lib/utils'

/**
 * Sole intentional action button.
 * Primary fill only via variant="default" (adds `.btn` for legacy CSS hooks).
 * Never rely on bare <button> becoming primary.
 */
const buttonVariants = cva(
  [
    'inline-flex items-center justify-center gap-1.5',
    'rounded-[var(--radius)] text-[length:var(--font-size-ui)] font-semibold leading-tight',
    'transition-[background,border-color,color] duration-150',
    'focus-visible:outline focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-[#9bb9e8]',
    'disabled:cursor-not-allowed disabled:opacity-45',
    'transform-none border border-solid box-border',
  ].join(' '),
  {
    variants: {
      variant: {
        default:
          'btn border-primary bg-primary text-white hover:border-primary-hover hover:bg-primary-hover',
        secondary:
          'secondary border-border-strong bg-surface text-text-secondary hover:bg-primary-soft hover:border-[#9bb9e8] hover:text-primary',
        danger: 'danger border-[#9f1f28] bg-danger text-white hover:bg-[#9f1f28]',
        ghost:
          'border-transparent bg-transparent text-text-secondary hover:bg-surface-2 hover:text-text shadow-none',
        outline:
          'border-border bg-surface text-text hover:border-primary hover:text-primary',
      },
      size: {
        /** Default = density scale control-h (32px) */
        default: 'h-8 min-h-8 max-h-8 px-3',
        dense: 'dense h-7 min-h-7 max-h-7 px-2.5 text-[length:var(--font-size-label)]',
        sm: 'h-7 min-h-7 max-h-7 px-2 text-[length:var(--font-size-label)]',
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
