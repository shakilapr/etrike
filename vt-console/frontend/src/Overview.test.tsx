import { render, screen } from '@testing-library/react'
import { beforeEach, describe, expect, it } from 'vitest'
import { Overview } from './components/Overview'
import { useAppStore } from './store'
import type { MessageState } from './types'

function msg(partial: Partial<MessageState>): MessageState {
  return {
    bus: 'high',
    can_id: 0x100,
    key: null,
    name: null,
    last_seen_ns: null,
    observed_rate_hz: null,
    expected_rate_hz: null,
    freshness: 'unseen',
    validation_status: null,
    signals: {},
    ...partial,
  }
}

describe('Overview', () => {
  beforeEach(() => {
    useAppStore.setState({ messages: [] })
  })

  it('shows ESTOP as Unknown with no sys:sys_safety_sts message yet', () => {
    render(<Overview />)
    expect(screen.getAllByText('Unknown').length).toBeGreaterThan(0)
  })

  it('renders ESTOP active/clear and CAN health counts from real freshness states', () => {
    useAppStore.setState({
      messages: [
        msg({
          key: 'sys:sys_safety_sts',
          bus: 'low',
          can_id: 0x011,
          freshness: 'live',
          signals: { estop_active: { raw_value: 1, engineering_value: 1, unit: null, enum_label: null, valid: true } },
        }),
        msg({ key: 'rt:rt_heartbeat', bus: 'high', can_id: 0x7fd, freshness: 'late' }),
        msg({ key: 'mtr:mtr_motor_fbk', bus: 'low', can_id: 0x206, freshness: 'missing' }),
      ],
    })
    render(<Overview />)

    expect(screen.getByText('Active')).toBeInTheDocument()
    expect(screen.getByText(/1 live/)).toBeInTheDocument()
    expect(screen.getByText(/1 late/)).toBeInTheDocument()
    expect(screen.getByText(/1 missing/)).toBeInTheDocument()
  })
})
