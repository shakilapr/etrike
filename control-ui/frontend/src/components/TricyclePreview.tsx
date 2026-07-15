import React, { useMemo } from 'react';
import { useCanStore, CanFrameState } from '../store/useCanStore';
import { Scan } from 'lucide-react';
import { clsx } from 'clsx';
import { twMerge } from 'tailwind-merge';

function cn(...inputs: (string | undefined | null | false)[]) {
  return twMerge(clsx(inputs));
}

function findFrameByMessageKey(channels: Record<string, Record<string, CanFrameState>>, messageKey: string): CanFrameState | null {
  for (const ch in channels) {
    for (const id in channels[ch]) {
      if (channels[ch][id].message_key === messageKey) {
        return channels[ch][id];
      }
    }
  }
  return null;
}

export function TricyclePreview() {
  const channels = useCanStore(state => state.channels);

  // Derive hardware feedback states
  const sesStatus = findFrameByMessageKey(channels, 'ses:ses_status');
  const mtrStatus = findFrameByMessageKey(channels, 'mtr:mtr_motor_fbk');

  const steeringAngleRad = (sesStatus?.signals?.steering_angle_mrad as number | undefined) ? (sesStatus.signals.steering_angle_mrad as number) / 1000 : 0;
  
  // Just for visual spinning of the wheels based on speed
  const speedMmps = (mtrStatus?.signals?.speed_mmps as number | undefined) || 0;
  
  // Map steering angle (-0.5 to 0.5 rad roughly) to degrees for CSS rotation
  const steeringDeg = steeringAngleRad * (180 / Math.PI);

  // Neon glowing wireframe aesthetic
  return (
    <div className="glass-panel w-full max-w-md mx-auto aspect-square flex flex-col relative overflow-hidden">
      <div className="bg-surface/50 border-b border-white/10 px-6 py-4 z-10">
        <h2 className="text-lg font-semibold flex items-center gap-2">
          <Scan className="text-primary" size={20} />
          Telemetry Preview
        </h2>
      </div>
      
      <div className="flex-1 flex items-center justify-center relative p-8">
        {/* Background grid for context */}
        <div className="absolute inset-0 bg-[linear-gradient(rgba(255,255,255,0.02)_1px,transparent_1px),linear-gradient(90deg,rgba(255,255,255,0.02)_1px,transparent_1px)] bg-[size:20px_20px]" />
        
        {/* SVG Drawing */}
        <svg viewBox="-100 -100 200 200" className="w-full h-full drop-shadow-[0_0_8px_rgba(56,189,248,0.5)] z-10" style={{ filter: "drop-shadow(0px 0px 8px rgba(56,189,248,0.5))" }}>
          <defs>
            <linearGradient id="neonGradient" x1="0%" y1="0%" x2="100%" y2="100%">
              <stop offset="0%" stopColor="#38bdf8" />
              <stop offset="100%" stopColor="#818cf8" />
            </linearGradient>
            <filter id="glow">
              <feGaussianBlur stdDeviation="3" result="coloredBlur"/>
              <feMerge>
                <feMergeNode in="coloredBlur"/>
                <feMergeNode in="SourceGraphic"/>
              </feMerge>
            </filter>
          </defs>

          {/* Chassis */}
          <g transform="translate(0, -20)">
            <path 
              d="M 0 -50 L 40 40 L -40 40 Z" 
              fill="none" 
              stroke="url(#neonGradient)" 
              strokeWidth="3" 
              filter="url(#glow)"
              className="opacity-80"
            />
            {/* Front Axle */}
            <line x1="0" y1="-50" x2="0" y2="-70" stroke="url(#neonGradient)" strokeWidth="3" filter="url(#glow)" />
            {/* Rear Axle */}
            <line x1="-40" y1="40" x2="40" y2="40" stroke="url(#neonGradient)" strokeWidth="3" filter="url(#glow)" />
          </g>

          {/* Wheels */}
          {/* Front Wheel (Steerable) */}
          <g transform={`translate(0, -90) rotate(${steeringDeg})`}>
            <rect x="-6" y="-15" width="12" height="30" rx="3" fill="none" stroke="#38bdf8" strokeWidth="2" filter="url(#glow)" />
            {/* Spin indicator based on speed */}
            <line x1="0" y1="-15" x2="0" y2="15" stroke="#818cf8" strokeWidth="1" strokeDasharray={speedMmps !== 0 ? "4 4" : "0"} className={speedMmps !== 0 ? "animate-pulse" : ""} />
          </g>

          {/* Left Rear Wheel */}
          <g transform="translate(-45, 20)">
            <rect x="-6" y="-15" width="12" height="30" rx="3" fill="none" stroke="#38bdf8" strokeWidth="2" filter="url(#glow)" />
          </g>

          {/* Right Rear Wheel */}
          <g transform="translate(45, 20)">
            <rect x="-6" y="-15" width="12" height="30" rx="3" fill="none" stroke="#38bdf8" strokeWidth="2" filter="url(#glow)" />
          </g>
        </svg>

        {/* Data Overlays */}
        <div className="absolute bottom-4 left-4 flex flex-col gap-1 text-xs font-mono">
          <div className="flex gap-2">
            <span className="text-text-dim">Steering:</span>
            <span className={steeringAngleRad === 0 ? "text-text" : "text-primary font-bold"}>{(steeringAngleRad * 1000).toFixed(1)} mrad</span>
          </div>
          <div className="flex gap-2">
            <span className="text-text-dim">Speed:</span>
            <span className={speedMmps === 0 ? "text-text" : "text-accent font-bold"}>{speedMmps.toFixed(0)} mm/s</span>
          </div>
        </div>
      </div>
    </div>
  );
}
