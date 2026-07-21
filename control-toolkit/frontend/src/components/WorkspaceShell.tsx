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
        <h1 className="m-0 text-[22px] font-bold tracking-tight text-text">{title}</h1>
        {description != null && description !== '' && (
          <p className="muted mt-1 max-w-[72ch] text-[13px] text-text-secondary">{description}</p>
        )}
        {headerExtra}
      </header>
      {children}
    </div>
  )
}
