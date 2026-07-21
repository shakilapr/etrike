import type { ReactNode } from 'react'
import type { Workspace } from '../store'

type NavItem = {
  id: Workspace
  label: string
  icon: ReactNode
}

type NavSection = { label: string; items: NavItem[] }

export function NavIcon({ children }: { children: ReactNode }) {
  return (
    <svg
      viewBox="0 0 24 24"
      width="16"
      height="16"
      stroke="currentColor"
      strokeWidth="2"
      fill="none"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden
    >
      {children}
    </svg>
  )
}

/** Lucide layout-grid */
export function IconLayoutGrid() {
  return (
    <NavIcon>
      <rect x="3" y="3" width="7" height="7" rx="1" />
      <rect x="14" y="3" width="7" height="7" rx="1" />
      <rect x="3" y="14" width="7" height="7" rx="1" />
      <rect x="14" y="14" width="7" height="7" rx="1" />
    </NavIcon>
  )
}

/** Lucide share-2 */
export function IconShare2() {
  return (
    <NavIcon>
      <circle cx="18" cy="5" r="3" />
      <circle cx="6" cy="12" r="3" />
      <circle cx="18" cy="19" r="3" />
      <path d="M8.59 13.51 15.42 17.49" />
      <path d="M15.41 6.51 8.59 10.49" />
    </NavIcon>
  )
}

/** Lucide activity */
export function IconActivity() {
  return (
    <NavIcon>
      <path d="M22 12h-4l-3 9L9 3l-3 9H2" />
    </NavIcon>
  )
}

/** Lucide sliders-horizontal */
export function IconSliders() {
  return (
    <NavIcon>
      <path d="M10 5H3" />
      <path d="M21 5h-7" />
      <path d="M14 19H3" />
      <path d="M21 19h-3" />
      <path d="M12 12H3" />
      <path d="M21 12h-5" />
      <circle cx="12" cy="5" r="2" />
      <circle cx="18" cy="12" r="2" />
      <circle cx="16" cy="19" r="2" />
    </NavIcon>
  )
}

/** Lucide gauge */
export function IconGauge() {
  return (
    <NavIcon>
      <path d="m12 14 4-4" />
      <path d="M3.34 19a10 10 0 1 1 17.32 0" />
    </NavIcon>
  )
}

/** Lucide terminal */
export function IconTerminal() {
  return (
    <NavIcon>
      <path d="M4 17 10 11 4 5" />
      <path d="M12 19h8" />
    </NavIcon>
  )
}

/** Lucide book */
export function IconBook() {
  return (
    <NavIcon>
      <path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20" />
      <path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z" />
    </NavIcon>
  )
}

/** Lucide search */
export function IconSearch() {
  return (
    <NavIcon>
      <circle cx="11" cy="11" r="8" />
      <path d="m21 21-4.3-4.3" />
    </NavIcon>
  )
}

/** Lucide scroll-text (logs) */
export function IconLogs() {
  return (
    <NavIcon>
      <path d="M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01" />
    </NavIcon>
  )
}

/** Lucide settings (gear) */
export function IconSettings() {
  return (
    <NavIcon>
      <path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z" />
      <circle cx="12" cy="12" r="3" />
    </NavIcon>
  )
}

/** Lucide external-link */
export function IconExternalLink() {
  return (
    <NavIcon>
      <path d="M15 3h6v6" />
      <path d="M10 14 21 3" />
      <path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6" />
    </NavIcon>
  )
}

/** Lucide monitor — Computer / virtual */
export function IconMonitor() {
  return (
    <svg
      viewBox="0 0 24 24"
      width="14"
      height="14"
      stroke="currentColor"
      strokeWidth="2"
      fill="none"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden
    >
      <rect x="2" y="3" width="20" height="14" rx="2" />
      <path d="M8 21h8" />
      <path d="M12 17v4" />
    </svg>
  )
}

/** Lucide cable — Real / CANalyst */
export function IconCable() {
  return (
    <svg
      viewBox="0 0 24 24"
      width="14"
      height="14"
      stroke="currentColor"
      strokeWidth="2"
      fill="none"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden
    >
      <path d="M17 21v-2a1 1 0 0 1-1-1v-1a2 2 0 0 1 2-2h2a2 2 0 0 1 2 2v1a1 1 0 0 1-1 1" />
      <path d="M19 15V6.5a1 1 0 0 0-7 0v11a1 1 0 0 1-7 0V9" />
      <path d="M21 21v-2h-4" />
      <path d="M3 5h4V3" />
      <path d="M7 5a1 1 0 0 1 1 1v1a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V6a1 1 0 0 1 1-1V3" />
    </svg>
  )
}

export const NAV_SECTIONS: NavSection[] = [
  {
    label: 'Observe',
    items: [
      { id: 'overview', label: 'Overview', icon: <IconLayoutGrid /> },
      { id: 'network', label: 'Network', icon: <IconShare2 /> },
      { id: 'live', label: 'Live CAN', icon: <IconActivity /> },
    ],
  },
  {
    label: 'Operate',
    items: [
      { id: 'control', label: 'Control', icon: <IconSliders /> },
      { id: 'preview', label: 'Drive', icon: <IconGauge /> },
    ],
  },
  {
    label: 'Analysis',
    items: [
      { id: 'bench', label: 'Bench', icon: <IconTerminal /> },
      { id: 'dictionary', label: 'Dictionary', icon: <IconBook /> },
      { id: 'diagnostics', label: 'Diagnostics', icon: <IconSearch /> },
      { id: 'logs', label: 'Logging', icon: <IconLogs /> },
    ],
  },
  {
    label: 'System',
    items: [{ id: 'settings', label: 'Settings', icon: <IconSettings /> }],
  },
]
