import React, { useRef, useEffect } from 'react';
import { useCanStore } from '../store/useCanStore';

type NavPage = 'monitor' | 'control' | 'settings';

interface SidebarProps {
  activePage: NavPage;
  onNavigate: (page: NavPage) => void;
  onQuickDrive: (dir: 'forward' | 'backward' | 'left' | 'right' | 'stop') => void;
  activeDir: Set<string>;
  keyboardEnabled: boolean;
  pressedKeys: { forward: boolean; backward: boolean; left: boolean; right: boolean };
  onToggleKeyboard: () => void;
}

import { TricyclePreview } from './TricyclePreview';

import { globalRobotState, drawWorldGrid, drawVehicle, setRobotTargets } from '../utils/robotDrawing';



export function Sidebar({ activePage, onNavigate, onQuickDrive, activeDir, keyboardEnabled, pressedKeys, onToggleKeyboard }: SidebarProps) {
  const channels = useCanStore(s => s.channels);
  const connected = useCanStore(s => s.connected);

  // Read steering from SES_STATUS (0x201) and speed from MTR_MOTOR_FBK (0x206)
  const sesFrame = channels['0']?.['0x201'] || channels['1']?.['0x201'];
  const mtrFrame = channels['0']?.['0x206'] || channels['1']?.['0x206'];

  const steerDeg = (sesFrame?.signals?.['angle_deg'] as number) || 0;
  const speedMmps = (mtrFrame?.signals?.['actual_speed_mmps'] as number) || 0;

  return (
    <aside className="sidebar" aria-label="Primary navigation" id="sidebar">
      <nav className="sidebar-nav">
        <div className="nav-section">
          <p className="nav-label">Workspace</p>
          <button
            className={`nav-item ${activePage === 'monitor' ? 'active' : ''}`}
            id="nav-monitor"
            onClick={() => onNavigate('monitor')}
          >
            <svg viewBox="0 0 24 24" width="16" height="16" stroke="currentColor" strokeWidth="1.8" fill="none">
              <path d="M3 12h4l2.2-5 4.2 10 2.2-5H21"/>
            </svg>
            <span>CAN Monitor</span>
          </button>
          <button
            className={`nav-item ${activePage === 'control' ? 'active' : ''}`}
            id="nav-control"
            onClick={() => onNavigate('control')}
          >
            <svg viewBox="0 0 24 24" width="16" height="16" stroke="currentColor" strokeWidth="1.8" fill="none">
              <rect x="7" y="7" width="10" height="10" rx="2"/>
              <path d="M9 1v3M15 1v3M9 20v3M15 20v3M20 9h3M20 14h3M1 9h3M1 14h3"/>
              <rect x="10" y="10" width="4" height="4" rx=".5"/>
            </svg>
            <span>Robot Control</span>
          </button>
          <button
            className={`nav-item ${activePage === 'settings' ? 'active' : ''}`}
            id="nav-settings"
            onClick={() => onNavigate('settings')}
          >
            <svg viewBox="0 0 24 24" width="16" height="16" stroke="currentColor" strokeWidth="1.8" fill="none">
              <circle cx="12" cy="12" r="3"></circle>
              <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z"></path>
            </svg>
            <span>Settings</span>
          </button>
        </div>
      </nav>

      {/* Robot Card */}
      <section className="robot-card" aria-labelledby="sidebot-title">
        <div className="robot-card-header">
          <div className="robot-card-title">
            <svg viewBox="0 0 24 24" width="14" height="14" stroke="var(--primary)" strokeWidth="1.8" fill="none">
              <rect x="7" y="7" width="10" height="10" rx="2"/>
              <path d="M9 1v3M15 1v3M9 20v3M15 20v3M20 9h3M20 14h3M1 9h3M1 14h3"/>
            </svg>
            <div>
              <strong id="sidebot-title">eTrike Robot</strong>
              <small>Tricycle model</small>
            </div>
          </div>
          {connected
            ? <span className="robot-live">Live</span>
            : <span style={{ fontSize: 10, color: 'var(--text-tertiary)', fontWeight: 600 }}>Offline</span>
          }
        </div>

        <div className="robot-stage" style={{ height: '400px', padding: 0 }}>
          <TricyclePreview 
            keyboardEnabled={keyboardEnabled}
            pressedKeys={pressedKeys}
            onToggleKeyboard={onToggleKeyboard}
          />
        </div>

        <div className="robot-readouts">
          <div className="robot-readout">
            <span>Speed</span>
            <strong id="sidebar-speed">{speedMmps.toFixed(0)} mm/s</strong>
          </div>
          <div className="robot-readout">
            <span>Steering</span>
            <strong id="sidebar-steer">{steerDeg.toFixed(1)}°</strong>
          </div>
        </div>

        <div className="robot-sidebar-controls" aria-label="Robot controls">
          <button className={`robot-control-button ${activeDir.has('left') ? 'is-pressed' : ''}`}
            onMouseDown={() => onQuickDrive('left')} onMouseUp={() => onQuickDrive('stop')}
            onTouchStart={() => onQuickDrive('left')} onTouchEnd={() => onQuickDrive('stop')}
            title="Steer left"><span>←</span></button>
          <button className={`robot-control-button ${activeDir.has('forward') ? 'is-pressed' : ''}`}
            onMouseDown={() => onQuickDrive('forward')} onMouseUp={() => onQuickDrive('stop')}
            onTouchStart={() => onQuickDrive('forward')} onTouchEnd={() => onQuickDrive('stop')}
            title="Accelerate"><span>↑</span></button>
          <button className={`robot-control-button ${activeDir.has('right') ? 'is-pressed' : ''}`}
            onMouseDown={() => onQuickDrive('right')} onMouseUp={() => onQuickDrive('stop')}
            onTouchStart={() => onQuickDrive('right')} onTouchEnd={() => onQuickDrive('stop')}
            title="Steer right"><span>→</span></button>
          <button className={`robot-control-button ${activeDir.has('backward') ? 'is-pressed' : ''}`}
            onMouseDown={() => onQuickDrive('backward')} onMouseUp={() => onQuickDrive('stop')}
            onTouchStart={() => onQuickDrive('backward')} onTouchEnd={() => onQuickDrive('stop')}
            title="Reverse"><span>↓</span></button>
          <button className="robot-control-button stop" onClick={() => onQuickDrive('stop')} title="Emergency stop"><span>Stop</span></button>
          <button className="robot-preview-link" onClick={() => onNavigate('control')}>
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" width="16" height="16">
              <rect x="3" y="4" width="18" height="16" rx="2" />
              <path d="M9 4v16" />
            </svg>
            <span>Open full preview</span>
          </button>
        </div>
      </section>

      <div className="system-card">
        <div className="system-card-row">
          <span className={`status-dot ${connected ? 'success' : 'danger'}`} />
          <div>
            <strong>{connected ? 'CANalyst Connected' : 'Not Connected'}</strong>
            <small>{connected ? 'Streaming on CH0+CH1' : 'No adapter detected'}</small>
          </div>
        </div>
      </div>
    </aside>
  );
}
