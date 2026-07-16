import clsx from 'clsx'
import { useAppStore, type Workspace } from '../store'

const ITEMS: { id: Workspace; label: string }[] = [
  { id: 'overview', label: 'Overview' },
  { id: 'network', label: 'Network' },
  { id: 'live', label: 'Live CAN' },
]

export function NavRail() {
  const workspace = useAppStore((s) => s.workspace)
  const setWorkspace = useAppStore((s) => s.setWorkspace)

  return (
    <nav className="flex w-44 shrink-0 flex-col gap-0.5 border-r border-border bg-surface p-2">
      {ITEMS.map((item) => (
        <button
          key={item.id}
          onClick={() => setWorkspace(item.id)}
          className={clsx(
            'rounded px-3 py-2 text-left text-sm transition-colors',
            workspace === item.id
              ? 'bg-surface-raised text-text'
              : 'text-text-dim hover:bg-surface-hover hover:text-text',
          )}
        >
          {item.label}
        </button>
      ))}
    </nav>
  )
}
