import React from 'react';
import { Activity, AlertTriangle } from 'lucide-react';
import { useWebSocket } from './hooks/useWebSocket';
import { useCanStore } from './store/useCanStore';
import { CanTable } from './components/CanTable';
import { ControlSidebar } from './components/ControlSidebar';
import { TricyclePreview } from './components/TricyclePreview';
import { clsx } from 'clsx';
import { twMerge } from 'tailwind-merge';

function cn(...inputs: (string | undefined | null | false)[]) {
  return twMerge(clsx(inputs));
}

function TopBar({ status }: { status: string }) {
  const connected = useCanStore(state => state.connected);
  const systemError = useCanStore(state => state.systemError);
  const dropped = useCanStore(state => state.droppedFrames);
  const errCount = useCanStore(state => state.errorFrameCount);

  return (
    <header className="sticky top-0 z-50 glass-panel rounded-none border-t-0 border-l-0 border-r-0 px-6 py-4 flex items-center justify-between mb-8">
      <div className="flex items-center gap-3">
        <Activity className="text-primary" />
        <div>
          <h1 className="text-xl font-bold tracking-tight">CANalyzer <span className="text-primary font-light">Control-UI</span></h1>
        </div>
      </div>

      <div className="flex items-center gap-6">
        {systemError && (
          <div className="flex items-center gap-2 text-error text-sm font-medium bg-error/10 px-3 py-1.5 rounded-full border border-error/20">
            <AlertTriangle size={16} />
            <span>{systemError}</span>
          </div>
        )}
        
        <div className="flex gap-4 text-sm text-text-dim">
          <div className="flex items-center gap-1.5">
            <span className="font-semibold text-text">{dropped}</span> Drops
          </div>
          <div className="flex items-center gap-1.5">
            <span className="font-semibold text-error">{errCount}</span> Errors
          </div>
        </div>

        <div className="h-6 w-px bg-white/10" />

        <div className="flex items-center gap-2">
          <div className="relative flex h-3 w-3">
            {status === 'connected' && connected && (
              <span className="animate-ping absolute inline-flex h-full w-full rounded-full bg-success opacity-75"></span>
            )}
            <span className={cn(
              "relative inline-flex rounded-full h-3 w-3",
              status === 'connected' && connected ? "bg-success" :
              status === 'connecting' ? "bg-accent" : "bg-error"
            )}></span>
          </div>
          <span className="text-sm font-medium text-text-dim capitalize">
            {status === 'connected' && connected ? 'Live' : status}
          </span>
        </div>
      </div>
    </header>
  );
}

function App() {
  const wsStatus = useWebSocket('ws://localhost:8000/api/stream');

  return (
    <div className="min-h-screen bg-background text-text selection:bg-primary/30">
      <TopBar status={wsStatus} />
      
      <main className="max-w-[1600px] mx-auto px-4 pb-20 flex gap-8 items-start">
        <ControlSidebar />
        
        <div className="flex-1 flex flex-col gap-8">
          <TricyclePreview />
          <CanTable />
        </div>
      </main>
    </div>
  );
}

export default App;
