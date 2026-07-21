import { useEffect, useRef } from 'react'
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
import { Settings } from './components/Settings'
import { Sidebar } from './components/Sidebar'
import { Topbar } from './components/Topbar'
import './App.css'

export default function App() {
  useBackendStream()
  const workspace = useAppStore((s) => s.workspace)
  const mainRef = useRef<HTMLElement>(null)
  useEffect(() => {
    mainRef.current?.scrollTo({ top: 0, left: 0 })
  }, [workspace])
  return (
    <div className="app flex h-screen flex-col overflow-hidden" data-testid="app">
      <Topbar />
      <div className="body flex min-h-0 flex-1">
        <Sidebar />
        <main ref={mainRef} className="min-h-0 min-w-0 flex-1 overflow-auto">
          {workspace === 'overview' && <Overview />}
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
        </main>
      </div>
    </div>
  )
}

