import { useEffect, useRef } from 'react'
import { getWorkspaceFromPath, syncUrlWithWorkspace } from './lib/routing'
import { useAppStore } from './store'
import { useBackendStream } from './useStream'
import { VehiclePreview } from './components/VehiclePreview'
import { CanDictionary } from './components/CanDictionary'
import { Bench } from './components/Bench'
import { Control } from './components/Control'
import { Diagnostics } from './components/Diagnostics'
import { Inject } from './components/Inject'
import { LiveCan } from './components/LiveCan'
import { Logs } from './components/Logs'
import { Network } from './components/Network'
import { Overview } from './components/Overview'
import { Dashboard } from './components/Dashboard'
import { Settings } from './components/Settings'
import { ActiveTxRail } from './components/ActiveTxRail'
import { Sidebar } from './components/Sidebar'
import { Topbar } from './components/Topbar'
import { UiKit } from './components/UiKit'
import { cn } from './lib/utils'
import './App.css'

export default function App() {
  useBackendStream()
  const workspace = useAppStore((s) => s.workspace)
  const setWorkspace = useAppStore((s) => s.setWorkspace)
  const mainRef = useRef<HTMLElement>(null)

  useEffect(() => {
    mainRef.current?.scrollTo({ top: 0, left: 0 })
  }, [workspace])

  useEffect(() => {
    // 1. Initial mount sync: if root '/' normalize to '/overview' with replaceState
    if (typeof window !== 'undefined') {
      const currentPath = window.location.pathname.replace(/\/+$/, '') || '/'
      if (currentPath === '/') {
        syncUrlWithWorkspace('overview', 'replace')
      } else {
        syncUrlWithWorkspace(workspace, 'replace')
      }
    }

    // 2. Handle browser Back / Forward (popstate)
    const handlePopState = () => {
      const nextWorkspace = getWorkspaceFromPath(window.location.pathname)
      setWorkspace(nextWorkspace, false)
    }

    window.addEventListener('popstate', handlePopState)
    return () => {
      window.removeEventListener('popstate', handlePopState)
    }
  }, [setWorkspace, workspace])
  return (
    <div className="app flex h-screen flex-col overflow-hidden" data-testid="app">
      <Topbar />
      <div className="body flex min-h-0 flex-1">
        <Sidebar />
        <main
          ref={mainRef}
          className={cn(
            'min-h-0 min-w-0 flex-1',
            workspace === 'dashboard' ? 'overflow-hidden' : 'overflow-auto',
          )}
        >
          {workspace === 'overview' && <Overview />}
          {workspace === 'dashboard' && <Dashboard />}
          {workspace === 'network' && <Network />}
          {workspace === 'live' && <LiveCan />}
          {workspace === 'control' && <Control />}
          {workspace === 'preview' && <VehiclePreview />}
          {workspace === 'bench' && <Bench />}
          {workspace === 'inject' && <Inject />}
          {workspace === 'dictionary' && <CanDictionary />}
          {workspace === 'diagnostics' && <Diagnostics />}
          {workspace === 'logs' && <Logs />}
          {workspace === 'settings' && <Settings />}
          {workspace === 'ui-kit' && <UiKit />}
        </main>
        {/* Active TX: open by default on Inject/Control; collapsible everywhere. */}
        <ActiveTxRail />
      </div>
    </div>
  )
}

