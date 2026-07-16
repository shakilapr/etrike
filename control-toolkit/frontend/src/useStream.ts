import { useEffect, useRef } from 'react'
import { useAppStore } from './store'
import { api } from './api'

function wsUrl() {
  const proto = window.location.protocol === 'https:' ? 'wss' : 'ws'
  return `${proto}://${window.location.host}/api/v1/stream`
}

export function useBackendStream() {
  const setStatus = useAppStore((s) => s.setStatus)
  const setMessages = useAppStore((s) => s.setMessages)
  const setTopology = useAppStore((s) => s.setTopology)
  const setStreamQuality = useAppStore((s) => s.setStreamQuality)
  const setReconnectAttempts = useAppStore((s) => s.setReconnectAttempts)
  const setProtocolMismatch = useAppStore((s) => s.setProtocolMismatch)
  const statusWireHash = useRef<string | null>(null)

  useEffect(() => {
    let closed = false
    let ws: WebSocket | null = null
    let retry = 0
    let timer: number | undefined
    let lastMsg = Date.now()
    let watch: number | undefined

    async function refresh() {
      try {
        const [st, topo] = await Promise.all([api.status(), api.topology()])
        if (closed) return
        statusWireHash.current = st.wire_hash
        setStatus(st)
        setTopology(topo.nodes || [])
      } catch {
        /* ignore */
      }
    }

    function connect() {
      setStreamQuality(retry === 0 ? 'connecting' : 'delayed')
      setReconnectAttempts(retry)
      ws = new WebSocket(wsUrl())
      ws.onopen = () => {
        retry = 0
        setReconnectAttempts(0)
        setStreamQuality('live')
        lastMsg = Date.now()
        void refresh()
      }
      ws.onmessage = (ev) => {
        lastMsg = Date.now()
        setStreamQuality('live')
        try {
          const msg = JSON.parse(ev.data as string) as {
            type: string
            wire_hash?: string
            messages?: unknown[]
            sequence?: number
          }
          if (msg.wire_hash && statusWireHash.current) {
            setProtocolMismatch(msg.wire_hash !== statusWireHash.current)
          }
          if (msg.type === 'hello' && msg.wire_hash && !statusWireHash.current) {
            statusWireHash.current = msg.wire_hash
          }
          if (msg.type === 'state' && Array.isArray(msg.messages)) {
            setMessages(msg.messages as never[], msg.sequence ?? 0)
          }
        } catch {
          /* ignore */
        }
      }
      ws.onclose = () => {
        if (closed) return
        setStreamQuality('lost')
        const delay = Math.min(8000, 500 * 2 ** retry)
        retry += 1
        setReconnectAttempts(retry)
        timer = window.setTimeout(connect, delay)
      }
      ws.onerror = () => ws?.close()
    }

    void refresh()
    connect()
    watch = window.setInterval(() => {
      const age = Date.now() - lastMsg
      if (age > 1500) setStreamQuality('lost')
      else if (age > 750) setStreamQuality('delayed')
    }, 250)
    const poll = window.setInterval(() => void refresh(), 2000)

    return () => {
      closed = true
      if (timer) window.clearTimeout(timer)
      if (watch) window.clearInterval(watch)
      window.clearInterval(poll)
      ws?.close()
    }
  }, [
    setMessages,
    setProtocolMismatch,
    setReconnectAttempts,
    setStatus,
    setStreamQuality,
    setTopology,
  ])
}
