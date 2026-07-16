import { render, screen } from '@testing-library/react'
import { beforeEach, describe, expect, it } from 'vitest'
import { Network } from './components/Network'
import { useAppStore } from './store'
import type { MessageState, ProtocolInstance } from './types'

const CATALOG: ProtocolInstance[] = [
  { key: 'rt:rt_heartbeat', name: 'RT_HEARTBEAT', bus: 'high', id: 0x7fd, frame_format: 'standard', sender: 'RT', receivers: ['SYS'], cycle_ms: 500, semantics: 'independent' },
  { key: 'rt:rt_heartbeat', name: 'RT_HEARTBEAT', bus: 'low', id: 0x7fd, frame_format: 'standard', sender: 'RT', receivers: ['SYS'], cycle_ms: 500, semantics: 'independent' },
  { key: 'sys:sys_heartbeat', name: 'SYS_HEARTBEAT', bus: 'low', id: 0x7fe, frame_format: 'standard', sender: 'SYS', receivers: ['RT'], cycle_ms: 100, semantics: 'independent' },
]

function state(key: string, bus: 'high' | 'low', can_id: number, freshness: MessageState['freshness']): MessageState {
  return { bus, can_id, key, name: null, last_seen_ns: null, observed_rate_hz: null, expected_rate_hz: null, freshness, validation_status: null, signals: {} }
}

describe('Network topology', () => {
  beforeEach(() => {
    useAppStore.setState({ catalog: [], messages: [], status: null })
  })

  it('groups catalog instances by declared sender and rolls up worst-case freshness', () => {
    useAppStore.setState({
      catalog: CATALOG,
      messages: [
        state('rt:rt_heartbeat', 'high', 0x7fd, 'live'),
        state('rt:rt_heartbeat', 'low', 0x7fd, 'late'),
        state('sys:sys_heartbeat', 'low', 0x7fe, 'missing'),
      ],
    })
    render(<Network />)

    expect(screen.getByText('RT')).toBeInTheDocument()
    expect(screen.getByText('SYS')).toBeInTheDocument()
    expect(screen.getByText('Late')).toBeInTheDocument() // RT worst-case: live + late -> Late
    expect(screen.getByText('Missing')).toBeInTheDocument() // SYS: missing
  })

  it('shows Unknown bus health when status has not loaded', () => {
    useAppStore.setState({ catalog: CATALOG, messages: [] })
    render(<Network />)
    expect(screen.getAllByText('Unknown').length).toBeGreaterThan(0)
  })
})
