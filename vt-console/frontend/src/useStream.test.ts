import { act, renderHook } from '@testing-library/react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { useBackendStream } from './useStream'
import { useAppStore } from './store'

class FakeWebSocket {
  static instances: FakeWebSocket[] = []
  onopen: (() => void) | null = null
  onmessage: ((ev: { data: string }) => void) | null = null
  onclose: (() => void) | null = null
  onerror: (() => void) | null = null
  sent: string[] = []
  url: string

  constructor(url: string) {
    this.url = url
    FakeWebSocket.instances.push(this)
  }

  send(data: string) {
    this.sent.push(data)
  }

  close() {
    this.onclose?.()
  }

  emit(msg: unknown) {
    this.onmessage?.({ data: JSON.stringify(msg) })
  }
}

describe('useBackendStream', () => {
  beforeEach(() => {
    FakeWebSocket.instances = []
    vi.stubGlobal('WebSocket', FakeWebSocket)
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({
        ok: true,
        json: async () => ({ session: { profile: 'pure_software', phase: 'running', bench_tx: 'disabled', session_id: null, test_session_id: null, revision: 0, adapter_epoch: null, wire_hash: null, destination: 'virtual', capabilities: [], leases: [] } }),
      }),
    )
    useAppStore.setState({ streamQuality: 'connecting', reconnectAttempts: 0, helloWireHash: null, messages: [], clockOffsetMs: null })
  })

  afterEach(() => {
    vi.unstubAllGlobals()
  })

  it('applies hello and a full state snapshot', () => {
    const { unmount } = renderHook(() => useBackendStream())
    const ws = FakeWebSocket.instances[0]
    ws.onopen?.()

    act(() => {
      ws.emit({ type: 'hello', wire_hash: 'abc', server_time_ns: 1_000_000_000 })
    })
    expect(useAppStore.getState().helloWireHash).toBe('abc')

    act(() => {
      ws.emit({ type: 'state', batch_seq: 1, version: 1, sequence: 1, messages: [{ bus: 'high', can_id: 1, key: null, name: null, last_seen_ns: null, observed_rate_hz: null, expected_rate_hz: null, freshness: 'live', validation_status: null, signals: {} }] })
    })
    expect(useAppStore.getState().messages).toHaveLength(1)

    unmount()
  })

  it('sends resync when a batch_seq gap is detected', () => {
    const { unmount } = renderHook(() => useBackendStream())
    const ws = FakeWebSocket.instances[0]
    ws.onopen?.()

    act(() => {
      ws.emit({ type: 'hello', wire_hash: 'abc', server_time_ns: 1 })
      ws.emit({ type: 'state', batch_seq: 1, version: 1, sequence: 1, messages: [] })
      ws.emit({ type: 'state', batch_seq: 5, version: 2, sequence: 2, messages: [] }) // gap: 2..4 missing
    })

    expect(ws.sent).toContainEqual(JSON.stringify({ type: 'resync' }))
    unmount()
  })

  it('reconnects with increasing attempt count after close', () => {
    vi.useFakeTimers()
    const { unmount } = renderHook(() => useBackendStream())
    const first = FakeWebSocket.instances[0]

    act(() => {
      first.close()
    })
    expect(useAppStore.getState().reconnectAttempts).toBe(1)

    act(() => {
      vi.advanceTimersByTime(1000)
    })
    expect(FakeWebSocket.instances.length).toBeGreaterThanOrEqual(2)

    unmount()
    vi.useRealTimers()
  })
})
