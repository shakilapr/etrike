import React from 'react';
import { useCanStore } from '../store/useCanStore';

export function MetricStrip() {
  const errorFrameCount = useCanStore(s => s.errorFrameCount);
  const droppedFrames = useCanStore(s => s.droppedFrames);
  const channels = useCanStore(s => s.channels);

  const totalMessages = Object.values(channels).reduce(
    (sum, ch) => sum + Object.keys(ch).length, 0
  );

  const totalFrames = Object.values(channels).reduce(
    (sum, ch) => Object.values(ch).reduce((s, f) => s + f.count, 0) + sum, 0
  );

  return (
    <div className="metric-strip" id="metric-strip">
      <div className="metric-item">
        <span>Total Frames</span>
        <strong>{totalFrames.toLocaleString()}</strong>
        <small>received since connect</small>
      </div>
      <div className="metric-item">
        <span>Unique IDs</span>
        <strong>{totalMessages}</strong>
        <small>CAN messages seen</small>
      </div>
      <div className={`metric-item ${errorFrameCount > 0 ? 'danger' : ''}`}>
        <span>Error Frames</span>
        <strong>{errorFrameCount}</strong>
        <small>bus error frames</small>
      </div>
      <div className={`metric-item ${droppedFrames > 0 ? 'danger' : ''}`}>
        <span>Dropped Frames</span>
        <strong>{droppedFrames}</strong>
        <small>queue overflows</small>
      </div>
    </div>
  );
}
