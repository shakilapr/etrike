import React, { useState } from 'react';
import { useTeleoperation } from '../hooks/useTeleoperation';
import { clsx } from 'clsx';
import { twMerge } from 'tailwind-merge';
import { Gamepad2, ShieldAlert } from 'lucide-react';

function cn(...inputs: (string | undefined | null | false)[]) {
  return twMerge(clsx(inputs));
}

function KeyButton({ label, active }: { label: string, active: boolean }) {
  return (
    <div className={cn(
      "w-12 h-12 flex items-center justify-center rounded-lg border-b-4 text-lg font-bold transition-all duration-100",
      active ? "bg-primary text-background border-primary transform translate-y-1" 
             : "bg-surface border-surface-hover text-text-dim"
    )}>
      {label}
    </div>
  );
}

export function ControlSidebar() {
  const [enabled, setEnabled] = useState(false);
  const keys = useTeleoperation(enabled);

  return (
    <div className="glass-panel w-72 flex flex-col h-[calc(100vh-140px)] sticky top-28 overflow-y-auto overflow-x-hidden">
      <div className="bg-surface/50 border-b border-white/10 px-6 py-4">
        <h2 className="text-lg font-semibold flex items-center gap-2">
          <Gamepad2 className="text-accent" size={20} />
          Teleoperation
        </h2>
      </div>

      <div className="p-6 flex flex-col gap-8 flex-1">
        <div className="flex flex-col gap-3">
          <p className="text-sm text-text-dim">
            Enable keyboard teleoperation to drive using WASD. 
            <br/><br/>
            <strong>Safety Interlock:</strong> Losing window focus will automatically stop the vehicle.
          </p>
          
          <button 
            onClick={() => setEnabled(!enabled)}
            className={cn(
              "py-3 rounded-lg font-semibold flex items-center justify-center gap-2 transition-colors duration-300",
              enabled ? "bg-error hover:bg-error/80 text-white" : "bg-primary hover:bg-primary/80 text-background"
            )}
          >
            {enabled ? (
              <><ShieldAlert size={18} /> Disable Control</>
            ) : (
              <><Gamepad2 size={18} /> Enable Control</>
            )}
          </button>
        </div>

        <div className={cn("flex flex-col items-center gap-2 transition-opacity duration-300", !enabled && "opacity-30 pointer-events-none")}>
          <KeyButton label="W" active={keys.forward} />
          <div className="flex gap-2">
            <KeyButton label="A" active={keys.left} />
            <KeyButton label="S" active={keys.backward} />
            <KeyButton label="D" active={keys.right} />
          </div>
        </div>
        
        {/* We can add Sliders here in the future if manual precise overrides are needed */}
      </div>
    </div>
  );
}
