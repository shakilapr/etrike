import type { ReactNode } from 'react'
import { cn } from '../lib/utils'

/** Shared workspace frame — Tailwind layout + legacy `.workspace` for gradual CSS migration. */
export function WorkspaceShell({
  testId,
  title,
  description,
  children,
  className,
  headerExtra,
}: {
  testId: string
  title: string
  description?: ReactNode
  children: ReactNode
  className?: string
  headerExtra?: ReactNode
}) {
  return (
    <div
      className={cn(
        'workspace flex max-w-[1600px] flex-col gap-4 px-[22px] py-5',
        className,
      )}
      data-testid={testId}
    >
      <header className="ws-header m-0">
        <h1 className="m-0 text-[length:var(--font-size-title)] font-bold tracking-tight text-text leading-tight">
          {title}
        </h1>
        {description != null && description !== '' && (
          <p className="muted mt-1 max-w-[72ch] text-[length:var(--font-size-ui)] text-text-secondary leading-snug">
            {description}
          </p>
        )}
        {headerExtra}
      </header>
      {children}
    </div>
  )
}
