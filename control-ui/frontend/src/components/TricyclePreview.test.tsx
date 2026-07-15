import React from 'react';
import { render, screen } from '@testing-library/react';
import { TricyclePreview } from './TricyclePreview';
import { useCanStore } from '../store/useCanStore';
import { describe, it, expect, beforeEach } from 'vitest';

describe('TricyclePreview', () => {
  beforeEach(() => {
    useCanStore.setState({ channels: { "0": {}, "1": {} } });
  });

  it('renders strictly from SES and MTR decoded values', () => {
    useCanStore.setState({
      channels: {
        "0": {
          "0x201": {
            id: "0x201",
            dlc: 8,
            data: "00",
            is_error: false,
            timestamp: 0,
            count: 1,
            age_ms: 0,
            delta_t_ms: 0,
            message_key: "ses:ses_status",
            signals: {
              steering_angle_mrad: -500 // -0.5 rad (approx -28 degrees)
            },
            _last_updated: 0,
            _just_changed: false
          },
          "0x206": {
            id: "0x206",
            dlc: 8,
            data: "00",
            is_error: false,
            timestamp: 0,
            count: 1,
            age_ms: 0,
            delta_t_ms: 0,
            message_key: "mtr:mtr_motor_fbk",
            signals: {
              speed_mmps: 1500
            },
            _last_updated: 0,
            _just_changed: false
          }
        }
      }
    });

    render(<TricyclePreview />);
    
    // Check overlays
    expect(screen.getByText('-500.0 mrad')).toBeInTheDocument();
    expect(screen.getByText('1500 mm/s')).toBeInTheDocument();
  });
});
