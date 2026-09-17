import type { ReactNode } from 'react'
import { useAppStore, type Activity } from '../store'
import {
  IconActivity,
  IconLayoutGrid,
  IconPanelLeftClose,
  IconPanelLeftOpen,
  IconSliders,
} from './icons'

const ACTIVITIES: Array<{
  id: Activity
  label: string
  icon: ReactNode
}> = [
  { id: 'explorer', label: 'Workspace explorer', icon: <IconLayoutGrid /> },
  { id: 'control', label: 'Control', icon: <IconSliders /> },
  { id: 'monitor', label: 'CAN monitor', icon: <IconActivity /> },
]

/** Horizontal/vertical activity switcher — sits at the top of the sidebar. */
export function ActivityBar() {
  const activity = useAppStore((s) => s.activity)
  const setActivity = useAppStore((s) => s.setActivity)
  const sidebarCollapsed = useAppStore((s) => s.sidebarCollapsed)
  const toggleSidebar = useAppStore((s) => s.toggleSidebar)

  return (
    <nav className="activity-bar" aria-label="Activity bar" data-testid="activity-bar">
      <div className="activity-bar-items">
        {ACTIVITIES.map((item) => (
          <button
            key={item.id}
            type="button"
            className={activity === item.id ? 'activity-btn active' : 'activity-btn'}
            aria-label={item.label}
            title={item.label}
            data-testid={`activity-${item.id}`}
            onClick={() => {
              // Activity icons swap contextual sidebar body.
              // If collapsed, clicking an activity also lets user switch.
              setActivity(item.id)
            }}
          >
            {item.icon}
          </button>
        ))}
      </div>
      <button
        type="button"
        className="activity-btn toggle-sidebar-btn"
        aria-label={sidebarCollapsed ? 'Expand sidebar (Ctrl+B)' : 'Collapse sidebar (Ctrl+B)'}
        title={sidebarCollapsed ? 'Expand sidebar (Ctrl+B)' : 'Collapse sidebar (Ctrl+B)'}
        data-testid="btn-toggle-sidebar"
        onClick={toggleSidebar}
      >
        {sidebarCollapsed ? <IconPanelLeftOpen /> : <IconPanelLeftClose />}
      </button>
    </nav>
  )
}
