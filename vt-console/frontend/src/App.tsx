import { useEffect } from 'react'
import { api } from './api'
import { useAppStore } from './store'
import { useBackendStream } from './useStream'
import { Header } from './components/Header'
import { NavRail } from './components/NavRail'
import { Overview } from './components/Overview'
import { Network } from './components/Network'
import { LiveCan } from './components/LiveCan'

export function App() {
  useBackendStream()
  const workspace = useAppStore((s) => s.workspace)
  const setStatus = useAppStore((s) => s.setStatus)
  const setCatalog = useAppStore((s) => s.setCatalog)

  useEffect(() => {
    let cancelled = false

    async function pollStatus() {
      try {
        const status = await api.status()
        if (!cancelled) setStatus(status)
      } catch {
        /* surfaced via streamQuality, not thrown to the UI */
      }
    }

    void pollStatus()
    const id = window.setInterval(() => void pollStatus(), 2000)

    void api
      .protocolMessages()
      .then((res) => {
        if (!cancelled) setCatalog(res.instances, res.wire_hash)
      })
      .catch(() => {
        /* catalog fetch failure leaves Network/Live CAN sender lookups as Unknown */
      })

    return () => {
      cancelled = true
      window.clearInterval(id)
    }
  }, [setStatus, setCatalog])

  return (
    <div className="flex h-full flex-col">
      <Header />
      <div className="flex min-h-0 flex-1">
        <NavRail />
        <main className="min-w-0 flex-1 overflow-auto">
          {workspace === 'overview' && <Overview />}
          {workspace === 'network' && <Network />}
          {workspace === 'live' && <LiveCan />}
        </main>
      </div>
    </div>
  )
}
