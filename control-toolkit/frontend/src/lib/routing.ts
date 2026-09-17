export type Workspace =
  | 'overview'
  | 'dashboard'
  | 'network'
  | 'live'
  | 'control'
  | 'preview'
  | 'bench'
  | 'inject'
  | 'dictionary'
  | 'diagnostics'
  | 'logs'
  | 'settings'
  | 'ui-kit'

export const VALID_WORKSPACES: readonly Workspace[] = [
  'overview',
  'dashboard',
  'network',
  'live',
  'control',
  'preview',
  'bench',
  'inject',
  'dictionary',
  'diagnostics',
  'logs',
  'settings',
  'ui-kit',
] as const

const PATH_TO_WORKSPACE: Record<string, Workspace> = {
  overview: 'overview',
  dashboard: 'dashboard',
  network: 'network',
  live: 'live',
  control: 'control',
  preview: 'preview',
  drive: 'preview',
  bench: 'bench',
  inject: 'inject',
  dictionary: 'dictionary',
  diagnostics: 'diagnostics',
  logs: 'logs',
  settings: 'settings',
  'ui-kit': 'ui-kit',
}

export const WORKSPACE_TITLES: Record<Workspace, string> = {
  overview: 'eTrike Control Toolkit — Overview',
  dashboard: 'eTrike Control Toolkit — Dashboard',
  network: 'eTrike Control Toolkit — Network Topology',
  live: 'eTrike Control Toolkit — Live CAN Bus',
  control: 'eTrike Control Toolkit — Manual Control',
  preview: 'eTrike Control Toolkit — Vehicle Drive Console',
  bench: 'eTrike Control Toolkit — Bench Mode & Mock Link',
  inject: 'eTrike Control Toolkit — Signal Injection',
  dictionary: 'eTrike Control Toolkit — Protocol Dictionary',
  diagnostics: 'eTrike Control Toolkit — Vehicle Diagnostics',
  logs: 'eTrike Control Toolkit — Telemetry & Flight Logs',
  settings: 'eTrike Control Toolkit — Settings & Configuration',
  'ui-kit': 'eTrike Control Toolkit — UI Component Kit',
}

/**
 * Determine the active Workspace from the current URL pathname.
 * Defaults to 'overview' for root path '/' or unrecognized routes.
 */
export function getWorkspaceFromPath(pathname?: string): Workspace {
  const raw =
    pathname !== undefined
      ? pathname
      : typeof window !== 'undefined'
        ? window.location.pathname
        : ''
  const trimmed = raw.replace(/^\/+|\/+$/g, '').trim().toLowerCase()
  const segment = trimmed.split('/')[0] ?? ''
  if (!segment) return 'overview'
  return PATH_TO_WORKSPACE[segment] ?? 'overview'
}

/**
 * Canonical URL path for a given workspace.
 */
export function getPathFromWorkspace(workspace: Workspace): string {
  return `/${workspace}`
}

/**
 * Update browser URL bar and history state for the given workspace.
 */
export function syncUrlWithWorkspace(
  workspace: Workspace,
  mode: 'push' | 'replace' | 'none' = 'push',
): void {
  if (typeof window === 'undefined') return

  if (mode !== 'none') {
    const targetPath = getPathFromWorkspace(workspace)
    const currentPath = window.location.pathname.replace(/\/+$/, '') || '/'

    if (currentPath === targetPath || (currentPath === '/' && targetPath === '/overview')) {
      if (currentPath === '/' && targetPath === '/overview') {
        window.history.replaceState(null, '', '/overview')
      }
    } else if (mode === 'replace') {
      window.history.replaceState(null, '', targetPath)
    } else {
      window.history.pushState(null, '', targetPath)
    }
  }

  // Keep browser document title in sync
  const title = WORKSPACE_TITLES[workspace]
  if (title && typeof document !== 'undefined') {
    document.title = title
  }
}
