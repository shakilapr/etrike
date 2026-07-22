import type { InputHTMLAttributes } from 'react'
import { cn } from '../../lib/utils'

export type InputProps = InputHTMLAttributes<HTMLInputElement> & {
  /** Dense toolbar search field (Live CAN filter, logs search). */
  search?: boolean
}

export function Input({ className, search, type = 'text', ...props }: InputProps) {
  return (
    <input
      type={type}
      className={cn(
        'rounded-[var(--radius)] border border-border-strong bg-surface text-text outline-none',
        'placeholder:text-text-tertiary',
        'focus:border-primary focus:shadow-[0_0_0_3px_rgba(31,95,191,0.11)]',
        'disabled:cursor-not-allowed disabled:opacity-50',
        'aria-invalid:border-danger',
        search
          ? 'search min-h-8 h-8 max-h-8 min-w-[200px] max-w-[400px] flex-1 px-2.5 text-[length:var(--font-size-ui)]'
          : 'h-8 min-h-8 max-h-8 px-2.5 text-[length:var(--font-size-ui)]',
        className,
      )}
      {...props}
    />
  )
}
