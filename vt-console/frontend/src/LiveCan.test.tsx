import { fireEvent, render, screen, within } from '@testing-library/react'
import { beforeEach, describe, expect, it } from 'vitest'
import { LiveCan } from './components/LiveCan'
import { useAppStore } from './store'
import type { MessageState } from './types'

function msg(partial: Partial<MessageState>): MessageState {
  return {
    bus: 'high',
    can_id: 0x300,
    key: 'host:host_drive_cmd',
    name: 'HOST_DRIVE_CMD',
    last_seen_ns: 0,
    observed_rate_hz: 10,
    expected_rate_hz: 10,
    freshness: 'live',
    validation_status: 'ok',
    signals: {},
    ...partial,
  }
}

describe('LiveCan', () => {
  beforeEach(() => {
    useAppStore.setState({ messages: [], catalog: [], clockOffsetMs: null })
  })

  it('renders one row per bus/id with name and freshness', () => {
    useAppStore.setState({ messages: [msg({})] })
    render(<LiveCan />)
    const table = within(screen.getByRole('table'))
    expect(table.getByText('HOST_DRIVE_CMD')).toBeInTheDocument()
    expect(table.getByText('0x300')).toBeInTheDocument()
    expect(table.getByText('Live')).toBeInTheDocument()
  })

  it('updates the same row in place when freshness changes, without adding a row', () => {
    const { rerender } = render(<LiveCan />)
    useAppStore.setState({ messages: [msg({ freshness: 'live' })] })
    rerender(<LiveCan />)
    const table = within(screen.getByRole('table'))
    expect(table.getAllByText('0x300')).toHaveLength(1)

    useAppStore.setState({ messages: [msg({ freshness: 'late' })] })
    rerender(<LiveCan />)
    expect(table.getAllByText('0x300')).toHaveLength(1)
    expect(table.getByText('Late')).toBeInTheDocument()
  })

  it('filters by bus', () => {
    useAppStore.setState({
      messages: [msg({ bus: 'high', can_id: 0x300 }), msg({ bus: 'low', can_id: 0x204, key: 'rt:rt_drive_cmd', name: 'RT_DRIVE_CMD' })],
    })
    render(<LiveCan />)
    expect(screen.getByText('2 / 2 messages')).toBeInTheDocument()

    fireEvent.change(screen.getByDisplayValue('All buses'), { target: { value: 'low' } })
    expect(screen.getByText('1 / 2 messages')).toBeInTheDocument()
    expect(screen.getByText('RT_DRIVE_CMD')).toBeInTheDocument()
    expect(screen.queryByText('HOST_DRIVE_CMD')).not.toBeInTheDocument()
  })
})
