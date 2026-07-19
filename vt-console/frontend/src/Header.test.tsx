import { render, screen } from '@testing-library/react'
import { beforeEach, describe, expect, it } from 'vitest'
import { Header } from './components/Header'
import { useAppStore } from './store'
import type { StatusResponse } from './types'

const STATUS: StatusResponse = {
  service: 'VTC',
  version: '0.1.0',
  ready: true,
  wire_hash: 'abc123',
  profile: 'pure_software',
  catalog: { messages: 32, instances: 42 },
  adapter: {
    identity: 'virtual',
    health: 'active',
    adapter_epoch: 1,
    capability: { hw_timestamps: null, tx_echo: null, listen_only: null, bus_off_reporting: null, tec_rec_reporting: null },
    channels: {
      high: { channel: 'high', activity: 'active', last_rx_ns: 1, rx_count: 10, tx_count: 0, rx_overflow: 0, queue_high_water: 1 },
      low: { channel: 'low', activity: 'quiet', last_rx_ns: null, rx_count: 0, tx_count: 0, rx_overflow: 0, queue_high_water: 0 },
    },
  },
  session: {
    profile: 'pure_software',
    phase: 'running',
    bench_tx: 'enabled',
    session_id: 'ses_1',
    test_session_id: 'test_1',
    revision: 3,
    adapter_epoch: 1,
    wire_hash: 'abc123',
    destination: 'virtual',
    capabilities: [],
    leases: [],
  },
}

describe('Header', () => {
  beforeEach(() => {
    useAppStore.setState({
      status: null,
      session: null,
      streamQuality: 'connecting',
      reconnectAttempts: 0,
      protocolMismatch: false,
      helloWireHash: null,
    })
  })

  it('renders adapter, bus, bench-tx, and stream-quality indicators', () => {
    useAppStore.getState().setStatus(STATUS)
    useAppStore.getState().setStreamQuality('live')
    render(<Header />)

    expect(screen.getByText('active')).toBeInTheDocument()
    expect(screen.getByText('High')).toBeInTheDocument()
    expect(screen.getByText('Low')).toBeInTheDocument()
    expect(screen.getByText('enabled')).toBeInTheDocument()
    expect(screen.getByText('live')).toBeInTheDocument()
    expect(screen.getByText('pure_software')).toBeInTheDocument()
  })

  it('shows a protocol hash mismatch badge when hello and status disagree', () => {
    useAppStore.getState().setStatus(STATUS)
    useAppStore.getState().setHelloWireHash('different-hash')
    render(<Header />)

    expect(screen.getByText('Protocol hash mismatch')).toBeInTheDocument()
  })
})
