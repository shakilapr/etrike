import React from 'react';
import { useCanStore, CanFrameState } from '../store/useCanStore';
import { AlertCircle, Clock, Zap } from 'lucide-react';
import { clsx } from 'clsx';
import { twMerge } from 'tailwind-merge';

function cn(...inputs: (string | undefined | null | false)[]) {
  return twMerge(clsx(inputs));
}

function formatHex(data: string, dlc: number) {
  // Add spaces between bytes
  const bytes = [];
  for (let i = 0; i < data.length; i += 2) {
    if (i / 2 < dlc) {
      bytes.push(data.substring(i, i + 2).toUpperCase());
    }
  }
  return bytes.join(' ');
}

function SignalDisplay({ signals }: { signals?: any }) {
  if (!signals) return <span className="text-text-dim text-sm italic">Raw</span>;
  
  return (
    <div className="flex flex-wrap gap-2">
      {Object.entries(signals).map(([key, value]) => (
        <span key={key} className="inline-flex items-center px-2 py-0.5 rounded text-xs font-medium bg-primary/10 text-primary border border-primary/20">
          <span className="opacity-75 mr-1">{key}:</span> {String(value)}
        </span>
      ))}
    </div>
  );
}

const FrameRow = React.memo(({ channel, can_id, frame }: { channel: string, can_id: string, frame: CanFrameState }) => {
  const isStale = frame.age_ms > 2000;
  
  return (
    <tr className={cn(
      "border-b border-white/5 transition-colors duration-300",
      frame.is_error && "bg-error/10 hover:bg-error/20",
      frame._just_changed && !frame.is_error && "bg-primary/20",
      !frame._just_changed && !frame.is_error && "hover:bg-white/5",
      isStale && "opacity-50"
    )}>
      <td className="py-3 px-4">
        <div className="flex items-center gap-2 font-mono text-sm">
          {frame.is_error ? <AlertCircle size={14} className="text-error" /> : <Zap size={14} className="text-accent" />}
          <span className={frame.is_error ? "text-error font-bold" : ""}>
            {can_id.toUpperCase()}
          </span>
        </div>
      </td>
      <td className="py-3 px-4 font-mono text-sm text-text-dim">
        {frame.message_key || 'Unknown'}
      </td>
      <td className="py-3 px-4 font-mono text-xs text-text-dim">
        {frame.dlc}
      </td>
      <td className="py-3 px-4 font-mono text-sm tracking-widest min-w-[140px]">
        {formatHex(frame.data, frame.dlc)}
      </td>
      <td className="py-3 px-4">
        <SignalDisplay signals={frame.signals} />
      </td>
      <td className="py-3 px-4">
        <div className="flex items-center gap-1.5 text-xs text-text-dim">
          <Clock size={12} />
          {frame.age_ms.toFixed(0)}ms
        </div>
      </td>
      <td className="py-3 px-4 text-xs font-mono text-text-dim text-right">
        {frame.count}
      </td>
    </tr>
  );
});

export function CanTable() {
  const channels = useCanStore(state => state.channels);

  return (
    <div className="flex flex-col gap-8 w-full max-w-7xl mx-auto p-4">
      {Object.entries(channels).map(([channel, frames]) => {
        const frameEntries = Object.entries(frames).sort(([idA], [idB]) => {
          return parseInt(idA, 16) - parseInt(idB, 16);
        });

        const busName = channel === "0" ? "High Bus" : channel === "1" ? "Low Bus" : `Channel ${channel}`;
        
        return (
          <div key={channel} className="glass-panel overflow-hidden">
            <div className="bg-surface/50 border-b border-white/10 px-6 py-4 flex items-center justify-between">
              <h2 className="text-lg font-semibold flex items-center gap-2">
                <div className={cn("w-2 h-2 rounded-full", frameEntries.length > 0 ? "bg-success animate-pulse" : "bg-text-dim")} />
                {busName}
              </h2>
              <span className="text-sm text-text-dim font-mono">{frameEntries.length} Unique IDs</span>
            </div>
            
            <div className="overflow-x-auto">
              <table className="w-full text-left border-collapse">
                <thead>
                  <tr className="border-b border-white/10 text-xs uppercase tracking-wider text-text-dim bg-surface/30">
                    <th className="py-3 px-4 font-medium">CAN ID</th>
                    <th className="py-3 px-4 font-medium">Protocol Key</th>
                    <th className="py-3 px-4 font-medium">DLC</th>
                    <th className="py-3 px-4 font-medium">Raw Payload (Hex)</th>
                    <th className="py-3 px-4 font-medium">Decoded Signals</th>
                    <th className="py-3 px-4 font-medium">Age</th>
                    <th className="py-3 px-4 font-medium text-right">Count</th>
                  </tr>
                </thead>
                <tbody>
                  {frameEntries.length === 0 ? (
                    <tr>
                      <td colSpan={7} className="py-12 text-center text-text-dim">
                        No traffic detected on this bus.
                      </td>
                    </tr>
                  ) : (
                    frameEntries.map(([can_id, frame]) => (
                      <FrameRow key={can_id} channel={channel} can_id={can_id} frame={frame} />
                    ))
                  )}
                </tbody>
              </table>
            </div>
          </div>
        );
      })}
    </div>
  );
}
