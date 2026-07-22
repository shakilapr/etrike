import type { ReactNode } from 'react'
import { useAppStore, type Activity } from '../store'
import { IconActivity, IconLayoutGrid, IconSliders } from './icons'

const ACTIVITIES: Array<{
  id: Activity
  label: string
  icon: ReactNode
}> = [
  { id: 'explorer', label: 'Workspace explorer', icon: <IconLayoutGrid /> },
  { id: 'control', label: 'All-node control', icon: <IconSliders /> },
  { id: 'monitor', label: 'CAN monitor', icon: <IconActivity /> },
]

export function ActivityBar() {
  const activity = useAppStore((s) => s.activity)
  const setActivity = useAppStore((s) => s.setActivity)

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
            // Activity rail only swaps the contextual sidebar — main workspace
            // stays put until the user picks a nav item (or control route).
            setActivity(item.id)
          }}
        >
          {item.icon}
        </button>
      ))}
    </nav>
  )
}
