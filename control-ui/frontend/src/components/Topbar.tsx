import React from 'react';
import { useCanStore } from '../store/useCanStore';

interface TopbarProps {
  wsStatus: string;
  frameRate: number;
  onToggleSidebar: () => void;
}

export function Topbar({ wsStatus, frameRate, onToggleSidebar }: TopbarProps) {
  const connected = useCanStore(s => s.connected);
  const systemError = useCanStore(s => s.systemError);
  const dropped = useCanStore(s => s.droppedFrames);
  const errCount = useCanStore(s => s.errorFrameCount);
  const channels = useCanStore(s => s.channels);

  const totalMessages = Object.values(channels).reduce(
    (sum, ch) => sum + Object.keys(ch).length, 0
  );

  const isLive = wsStatus === 'connected' && connected;
  const statusLabel = isLive ? 'Live' : wsStatus === 'connecting' ? 'Connecting' : 'Disconnected';
  const statusClass = isLive ? 'live' : wsStatus === 'connecting' ? 'connecting' : 'danger';

  return (
    <header className="topbar" id="topbar">
      <button
        className="icon-btn"
        onClick={onToggleSidebar}
        title="Toggle sidebar"
        style={{ border: 'none', background: 'transparent', cursor: 'pointer' }}
      >
        <svg viewBox="0 0 24 24" width="18" height="18" stroke="currentColor" strokeWidth="1.8" fill="none">
          <rect x="3" y="4" width="18" height="16" rx="2"/>
          <path d="M9 4v16"/>
        </svg>
      </button>

      <a className="topbar-brand" href="#" id="brand-link">
        <span className="topbar-brand-mark">eT</span>
        <span className="topbar-brand-name">eTrike Control</span>
      </a>
      <span className="env-badge">CAN Bench</span>

      <div className="topbar-spacer" />

      {systemError && (
        <div style={{
          display: 'flex', alignItems: 'center', gap: 6,
          background: 'var(--danger-soft)', color: 'var(--danger)',
          padding: '4px 10px', borderRadius: 999, fontSize: 12, fontWeight: 600,
          border: '1px solid rgba(186,45,54,.2)'
        }}>
          <svg viewBox="0 0 24 24" width="14" height="14" stroke="currentColor" strokeWidth="2" fill="none">
            <path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/>
            <line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/>
          </svg>
          {systemError}
        </div>
      )}

      <div className="topbar-stats">
        <div className="topbar-stat">
          <strong>{frameRate.toFixed(0)}</strong> fps
        </div>
        <div className="topbar-stat">
          <strong>{totalMessages}</strong> msgs
        </div>
        <div className={`topbar-stat ${errCount > 0 ? 'danger' : ''}`}>
          <strong>{errCount}</strong> errors
        </div>
        <div className={`topbar-stat ${dropped > 0 ? 'danger' : ''}`}>
          <strong>{dropped}</strong> drops
        </div>
      </div>

      <div className="topbar-divider" />

      <div className="topbar-status" id="ws-status">
        <span className={`status-dot ${statusClass}`} />
        <span style={{ fontSize: 13, color: 'var(--text-secondary)' }}>{statusLabel}</span>
      </div>
    </header>
  );
}
