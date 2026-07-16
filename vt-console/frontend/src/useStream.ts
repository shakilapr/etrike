import { useEffect } from 'react'
import { api } from './api'
import { useAppStore } from './store'
import type { WsFrame } from './types'

function wsUrl(): string {
  const proto = window.location.protocol === 'https:' ? 'wss' : 'ws'
  return `${proto}://${window.location.host}/api/v1/stream`
}

// Connects to vtc's WebSocket stream (vtc/api/stream.py): hello -> state
// (full snapshot, coalesced on the store's version counter) | heartbeat |
// event. Detects batch_seq gaps and asks the server to resync. Reconnects
// with exponential backoff. Stream quality decays on an independent client
// clock (workplan §4.3) so a stalled connection is visible even with no new
// frames arriving.
export function useBackendStream() {
  const setSession = useAppStore((s) => s.setSession)
  const setMessages = useAppStore((s) => s.setMessages)
  const setStreamQuality = useAppStore((s) => s.setStreamQuality)
  const setReconnectAttempts = useAppStore((s) => s.setReconnectAttempts)
  const setHelloWireHash = useAppStore((s) => s.setHelloWireHash)
  const setClockOffsetMs = useAppStore((s) => s.setClockOffsetMs)

  useEffect(() => {
    let closed = false
    let ws: WebSocket | null = null
    let retry = 0
    let reconnectTimer: number | undefined
    let lastMsgAt = Date.now()
    let lastBatchSeq = 0
    let qualityWatch: number | undefined

    function connect() {
      setStreamQuality(retry === 0 ? 'connecting' : 'delayed')
      setReconnectAttempts(retry)
      ws = new WebSocket(wsUrl())

      ws.onopen = () => {
        retry = 0
        setReconnectAttempts(0)
        lastMsgAt = Date.now()
        lastBatchSeq = 0
      }

      ws.onmessage = (ev) => {
        lastMsgAt = Date.now()
        setStreamQuality('live')
        let frame: WsFrame
        try {
          frame = JSON.parse(ev.data as string) as WsFrame
        } catch {
          return
        }

        if ('batch_seq' in frame) {
          if (lastBatchSeq !== 0 && frame.batch_seq !== lastBatchSeq + 1) {
            // Gap detected — ask for a fresh full snapshot next tick.
            ws?.send(JSON.stringify({ type: 'resync' }))
          }
          lastBatchSeq = frame.batch_seq
        }

        switch (frame.type) {
          case 'hello':
            setHelloWireHash(frame.wire_hash)
            setClockOffsetMs(Date.now() - frame.server_time_ns / 1e6)
            break
          case 'state':
            setMessages(frame.messages, frame.sequence)
            break
          case 'heartbeat':
            setClockOffsetMs(Date.now() - frame.server_time_ns / 1e6)
            break
          case 'event':
            // Critical-event fan-out; no dedicated UI surface yet (Phase 6
            // Diagnostics workspace owns event presentation).
            break
        }
      }

      ws.onclose = () => {
        if (closed) return
        setStreamQuality('lost')
        const delay = Math.min(8000, 500 * 2 ** retry)
        retry += 1
        setReconnectAttempts(retry)
        reconnectTimer = window.setTimeout(connect, delay)
      }

      ws.onerror = () => ws?.close()
    }

    connect()

    // Independent freshness clock: quality decays even with no new frames.
    qualityWatch = window.setInterval(() => {
      const age = Date.now() - lastMsgAt
      if (age > 1500) setStreamQuality('lost')
      else if (age > 750) setStreamQuality('delayed')
    }, 250)

    async function fetchSession() {
      try {
        const { session } = await api.sessions()
        setSession(session)
      } catch {
        /* status/session poll failure is surfaced via streamQuality, not thrown */
      }
    }
    void fetchSession()
    const sessionPoll = window.setInterval(() => void fetchSession(), 2000)

    return () => {
      closed = true
      if (reconnectTimer) window.clearTimeout(reconnectTimer)
      if (qualityWatch) window.clearInterval(qualityWatch)
      window.clearInterval(sessionPoll)
      ws?.close()
    }
  }, [setSession, setMessages, setStreamQuality, setReconnectAttempts, setHelloWireHash, setClockOffsetMs])
}
