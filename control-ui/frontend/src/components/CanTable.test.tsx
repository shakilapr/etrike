import React from 'react';
import { render, screen, act } from '@testing-library/react';
import { CanTable } from './CanTable';
import { useCanStore } from '../store/useCanStore';

describe('CanTable', () => {
  beforeEach(() => {
    useCanStore.setState({
      channels: { "0": {}, "1": {} }
    });
  });

  it('renders empty state correctly', () => {
    render(<CanTable />);
    expect(screen.getAllByText('No traffic detected on this bus.')).toHaveLength(2); // One for each channel
  });

  it('renders frames correctly', () => {
    useCanStore.setState({
      channels: {
        "0": {
          "0x300": {
            id: "0x300",
            dlc: 8,
            data: "0000000000000000",
            is_error: false,
            timestamp: 1000,
            count: 1,
            age_ms: 50,
            delta_t_ms: 10,
            message_key: "host_drive_cmd",
            _last_updated: 0,
            _just_changed: false
          }
        }
      }
    });

    render(<CanTable />);
    expect(screen.getByText('0X300')).toBeInTheDocument();
    expect(screen.getByText('host_drive_cmd')).toBeInTheDocument();
    expect(screen.getByText('00 00 00 00 00 00 00 00')).toBeInTheDocument();
  });
});
