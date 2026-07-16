import React, { useState, useCallback, useRef, useEffect } from 'react';
import { useWebSocket } from './hooks/useWebSocket';
import { useTeleoperation } from './hooks/useTeleoperation';
import { useCanStore, type CanFrameState } from './store/useCanStore';
import { sendHostDriveCmd } from './api/inject';

import { Topbar } from './components/Topbar';
import { Sidebar } from './components/Sidebar';
import { MetricStrip } from './components/MetricStrip';

import { CommandPanel } from './components/CommandPanel';
import { CanTable } from './components/CanTable';
import { DetailsPanel } from './components/DetailsPanel';
import { SettingsPanel } from './components/SettingsPanel';

import './index.css';

type NavPage = 'monitor' | 'control' | 'settings';

function useFrameRate(): number {
  const [fps, setFps] = useState(0);
  const channels = useCanStore(s => s.channels);
  const prevCountRef = useRef(0);

  useEffect(() => {
    const totalFrames = Object.values(channels).reduce(
      (sum, ch) => Object.values(ch).reduce((s, f) => s + f.count, 0) + sum, 0
    );
    const id = setInterval(() => {
      const now = Object.values(channels).reduce(
        (sum, ch) => Object.values(ch).reduce((s, f) => s + f.count, 0) + sum, 0
      );
      setFps((now - prevCountRef.current));
      prevCountRef.current = now;
    }, 1000);
    return () => clearInterval(id);
  }, [channels]);

  return fps;
}

export default function App() {
  const wsStatus = useWebSocket('ws://localhost:8000/api/stream');
  const frameRate = useFrameRate();

  const [sidebarCollapsed, setSidebarCollapsed] = useState(false);
  const [activePage, setActivePage] = useState<NavPage>('control');
  const [kbEnabled, setKbEnabled] = useState(false);
  const [selectedFrame, setSelectedFrame] = useState<(CanFrameState & { channel: string }) | null>(null);

  // Teleoperation
  const keys = useTeleoperation(kbEnabled);

  // Quick drive from sidebar buttons
  const [activeDir, setActiveDir] = useState<Set<string>>(new Set());
  const activeDirRef = useRef<Set<string>>(new Set());

  const handleQuickDrive = useCallback((dir: 'forward' | 'backward' | 'left' | 'right' | 'stop') => {
    if (dir === 'stop') {
      activeDirRef.current = new Set();
      setActiveDir(new Set());
      sendHostDriveCmd(0, 0, 1);
      return;
    }
    const next = new Set(activeDirRef.current);
    next.add(dir);
    activeDirRef.current = next;
    setActiveDir(new Set(next));

    const speed = next.has('forward') ? 1500 : next.has('backward') ? -800 : 0;
    const yaw = next.has('left') ? 600 : next.has('right') ? -600 : 0;
    sendHostDriveCmd(speed, yaw, 1);
  }, []);

  const handleSelectFrame = useCallback((frame: CanFrameState & { channel: string }) => {
    setSelectedFrame(frame);
  }, []);

  const handleCloseDetails = useCallback(() => {
    setSelectedFrame(null);
  }, []);

  const selectedId = selectedFrame
    ? `${selectedFrame.channel}:${selectedFrame.id}`
    : null;

  return (
    <div
      className={`app-shell ${sidebarCollapsed ? 'sidebar-collapsed' : ''} ${!selectedFrame ? 'details-hidden' : ''}`}
      id="app-shell"
    >
      <Topbar
        wsStatus={wsStatus}
        frameRate={frameRate}
        onToggleSidebar={() => setSidebarCollapsed(c => !c)}
      />

      <Sidebar
        activePage={activePage}
        onNavigate={setActivePage}
        onQuickDrive={handleQuickDrive}
        activeDir={activeDir}
        keyboardEnabled={kbEnabled}
        pressedKeys={keys}
        onToggleKeyboard={() => setKbEnabled(e => !e)}
      />

      <main className="main-content" id="main-content">
        <MetricStrip />

        {activePage === 'control' && (
          <>
            <CommandPanel />
          </>
        )}

        {activePage === 'monitor' && (
          <CanTable
            onSelectFrame={handleSelectFrame}
            selectedId={selectedId}
          />
        )}

        {activePage === 'settings' && (
          <SettingsPanel />
        )}

        {/* Always show table on control page below command panel */}
        {activePage === 'control' && (
          <CanTable
            onSelectFrame={handleSelectFrame}
            selectedId={selectedId}
          />
        )}
      </main>

      <DetailsPanel
        frame={selectedFrame}
        onClose={handleCloseDetails}
      />
    </div>
  );
}
