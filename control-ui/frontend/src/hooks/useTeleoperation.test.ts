import { renderHook, act } from '@testing-library/react';
import { useTeleoperation } from './useTeleoperation';
import { sendHostDriveCmd } from '../api/inject';
import { vi, describe, it, expect, beforeEach, afterEach } from 'vitest';

vi.mock('../api/inject', () => ({
  sendHostDriveCmd: vi.fn(),
}));

describe('useTeleoperation', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('sends zero command on disable', () => {
    renderHook(() => useTeleoperation(false));
    expect(sendHostDriveCmd).toHaveBeenCalledWith(0, 0, 1);
  });

  it('updates keys on keydown and keyup', () => {
    const { result } = renderHook(() => useTeleoperation(true));
    
    act(() => {
      window.dispatchEvent(new KeyboardEvent('keydown', { key: 'w' }));
    });
    expect(result.current.forward).toBe(true);
    
    act(() => {
      window.dispatchEvent(new KeyboardEvent('keyup', { key: 'w' }));
    });
    expect(result.current.forward).toBe(false);
  });

  it('sends zero command on blur', () => {
    renderHook(() => useTeleoperation(true));
    vi.clearAllMocks(); // Clear the initial periodic zeroes if any
    
    act(() => {
      window.dispatchEvent(new Event('blur'));
    });
    
    expect(sendHostDriveCmd).toHaveBeenCalledWith(0, 0, 1);
  });

  it('sends periodic commands while enabled', () => {
    renderHook(() => useTeleoperation(true));
    vi.clearAllMocks();
    
    act(() => {
      window.dispatchEvent(new KeyboardEvent('keydown', { key: 'w' }));
    });
    
    act(() => {
      // Advance 50ms interval
      vi.advanceTimersByTime(55);
    });
    
    expect(sendHostDriveCmd).toHaveBeenCalledWith(2000, 0, 1);
  });
});
