import { describe, it, expect, beforeEach, vi, afterEach } from 'vitest';
import { useCanStore } from './useCanStore';

describe('useCanStore', () => {
  beforeEach(() => {
    // Reset store before each test
    useCanStore.setState({
      connected: false,
      systemError: null,
      busError: null,
      errorFrameCount: 0,
      droppedFrames: 0,
      channels: { "0": {}, "1": {} }
    });
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('coalesces new frames and sets _just_changed', () => {
    const store = useCanStore.getState();
    
    const mockBatch = {
      connected: true,
      system_error: null,
      bus_error: null,
      error_frame_count: 0,
      dropped_frames: 0,
      channels: {
        "0": {
          "0x300": {
            id: "0x300",
            dlc: 8,
            data: "0000000000000000",
            is_error: false,
            timestamp: 1000,
            count: 1,
            age_ms: 0,
            delta_t_ms: 0,
            _last_updated: 0,
            _just_changed: false
          }
        }
      }
    };

    // First update should set _just_changed = true for new frames
    useCanStore.getState().updateBatch(mockBatch);
    const state1 = useCanStore.getState();
    expect(state1.channels["0"]["0x300"]._just_changed).toBe(true);
    
    // Simulate some time passing but no data change
    vi.advanceTimersByTime(500);
    const mockBatch2 = JSON.parse(JSON.stringify(mockBatch));
    mockBatch2.channels["0"]["0x300"].count = 2; // only count changes
    
    useCanStore.getState().updateBatch(mockBatch2);
    const state2 = useCanStore.getState();
    // It should STILL be highlighted because 500ms < 1500ms duration
    expect(state2.channels["0"]["0x300"]._just_changed).toBe(true);

    // Simulate more time passing so the highlight expires
    vi.advanceTimersByTime(2000);
    useCanStore.getState().ageFrames();
    
    const state3 = useCanStore.getState();
    expect(state3.channels["0"]["0x300"]._just_changed).toBe(false);
  });

  it('re-highlights frames when data changes', () => {
    const store = useCanStore.getState();
    
    const mockBatch = {
      connected: true,
      system_error: null,
      bus_error: null,
      error_frame_count: 0,
      dropped_frames: 0,
      channels: {
        "0": {
          "0x300": {
            id: "0x300",
            dlc: 8,
            data: "0000000000000000",
            is_error: false,
            timestamp: 1000,
            count: 1,
            age_ms: 0,
            delta_t_ms: 0,
            _last_updated: 0,
            _just_changed: false
          }
        }
      }
    };

    useCanStore.getState().updateBatch(mockBatch);
    
    // Expire the highlight
    vi.advanceTimersByTime(2000);
    useCanStore.getState().ageFrames();
    expect(useCanStore.getState().channels["0"]["0x300"]._just_changed).toBe(false);
    
    // Send new data
    const mockBatch2 = JSON.parse(JSON.stringify(mockBatch));
    mockBatch2.channels["0"]["0x300"].data = "FFFFFFFFFFFFFFFF";
    useCanStore.getState().updateBatch(mockBatch2);
    
    // Should highlight again!
    expect(useCanStore.getState().channels["0"]["0x300"]._just_changed).toBe(true);
  });
});
