import { create } from 'zustand';

export interface CanSignal {
  [key: string]: number | string | boolean;
}

export interface CanFrameState {
  id: string; // hex string e.g. "0x300"
  dlc: number;
  data: string; // hex string
  is_error: boolean;
  timestamp: number;
  count: number;
  age_ms: number;
  delta_t_ms: number;
  decode_status?: string | null;
  signals?: CanSignal | null;
  message_key?: string | null;
  _last_updated: number; // Internal timestamp for coalescing and aging
  _just_changed: boolean; // For micro-animations
}

interface UiBatch {
  connected: boolean;
  system_error: string | null;
  bus_error: any | null;
  error_frame_count: number;
  dropped_frames: number;
  channels: {
    [channel: string]: {
      [can_id: string]: CanFrameState;
    };
  };
}

interface CanStoreState {
  connected: boolean;
  systemError: string | null;
  busError: any | null;
  errorFrameCount: number;
  droppedFrames: number;
  channels: {
    [channel: string]: {
      [can_id: string]: CanFrameState;
    };
  };
  updateBatch: (batch: UiBatch) => void;
  ageFrames: () => void;
}

const STALE_THRESHOLD_MS = 2000;
const HIGHLIGHT_DURATION_MS = 1500;

export const useCanStore = create<CanStoreState>((set) => ({
  connected: false,
  systemError: null,
  busError: null,
  errorFrameCount: 0,
  droppedFrames: 0,
  channels: {
    "0": {},
    "1": {}
  },

  updateBatch: (batch) => set((state) => {
    const now = Date.now();
    const nextChannels = { ...state.channels };

    // Process channels from the incoming batch
    Object.keys(batch.channels).forEach(channel => {
      const channelData = batch.channels[channel];
      if (!nextChannels[channel]) nextChannels[channel] = {};
      
      const nextChannelState = { ...nextChannels[channel] };

      Object.keys(channelData).forEach(can_id => {
        const incomingFrame = channelData[can_id];
        const existingFrame = nextChannelState[can_id];
        
        let justChanged = false;
        
        // Detect value changes for highlighting (ignoring pure timestamp/count updates)
        if (existingFrame) {
          if (existingFrame.data !== incomingFrame.data || existingFrame.is_error !== incomingFrame.is_error) {
            justChanged = true;
          } else {
            // Keep the highlight if it hasn't expired yet
            if (existingFrame._just_changed && (now - existingFrame._last_updated < HIGHLIGHT_DURATION_MS)) {
              justChanged = true;
            }
          }
        } else {
          // New frames get highlighted immediately
          justChanged = true;
        }

        nextChannelState[can_id] = {
          ...incomingFrame,
          _last_updated: justChanged && existingFrame && existingFrame.data !== incomingFrame.data ? now : (existingFrame ? existingFrame._last_updated : now),
          _just_changed: justChanged,
        };
      });
      
      nextChannels[channel] = nextChannelState;
    });

    return {
      connected: batch.connected,
      systemError: batch.system_error,
      busError: batch.bus_error,
      errorFrameCount: batch.error_frame_count,
      droppedFrames: batch.dropped_frames,
      channels: nextChannels
    };
  }),

  ageFrames: () => set((state) => {
    const now = Date.now();
    let hasChanges = false;
    const nextChannels = { ...state.channels };

    Object.keys(nextChannels).forEach(channel => {
      let channelChanged = false;
      const nextChannelState = { ...nextChannels[channel] };

      Object.keys(nextChannelState).forEach(can_id => {
        const frame = nextChannelState[can_id];
        let updatedFrame = { ...frame };
        let frameChanged = false;
        
        // Update age visual
        if (updatedFrame._just_changed && (now - updatedFrame._last_updated > HIGHLIGHT_DURATION_MS)) {
          updatedFrame._just_changed = false;
          frameChanged = true;
        }
        
        // Mark stale if necessary
        if (updatedFrame.age_ms > STALE_THRESHOLD_MS && updatedFrame.decode_status !== "stale") {
          // We don't overwrite real decode statuses usually, but for UI purposes,
          // tracking staleness is helpful. Let's just rely on age_ms > threshold in components.
        }

        if (frameChanged) {
          nextChannelState[can_id] = updatedFrame;
          channelChanged = true;
        }
      });

      if (channelChanged) {
        nextChannels[channel] = nextChannelState;
        hasChanges = true;
      }
    });

    return hasChanges ? { channels: nextChannels } : state;
  })
}));
