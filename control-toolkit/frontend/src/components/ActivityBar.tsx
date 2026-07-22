import type { ReactNode } from 'react'
import { useAppStore, type Activity } from '../store'
import { IconActivity, IconLayoutGrid, IconSliders } from './icons'

const ACTIVITIES: Array<{
  id: Activity
  label: string
  workspace: 'overview' | 'control' | 'live'
  icon: ReactNode
}> = [
  { id: 'explorer', label: 'Workspace explorer', workspace: 'overview', icon: <IconLayoutGrid /> },
  { id: 'control', label: 'All-node control', workspace: 'control', icon: <IconSliders /> },
  { id: 'monitor', label: 'CAN monitor', workspace: 'live', icon: <IconActivity /> },
]

export function ActivityBar() {
  const activity = useAppStore((s) => s.activity)
  const setActivity = useAppStore((s) => s.setActivity)
  const setWorkspace = useAppStore((s) => s.setWorkspace)

  return (
    <nav className="activity-bar" aria-label="Activity bar" data-testid="activity-bar">
      {ACTIVITIES.map((item) => (
        <button
          key={item.id}
          type="button"
          className={activity === item.id ? 'activity-btn active' : 'activity-btn'}
          aria-label={item.label}
          title={item.label}
          data-testid={`activity-${item.id}`}
          onClick={() => {
            setActivity(item.id)
            setWorkspace(item.workspace)
          }}
        >
          {item.icon}
        </button>
      ))}
    </nav>
  )
}
