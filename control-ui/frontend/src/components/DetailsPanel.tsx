import React from 'react';
import type { CanFrameState } from '../store/useCanStore';

interface DetailsPanelProps {
  frame: (CanFrameState & { channel: string }) | null;
  onClose: () => void;
}

const BUS_NAMES: Record<string, string> = { '0': 'high', '1': 'low' };

function formatHex(hex: string): string {
  return hex.replace(/(.{2})/g, '$1 ').trim().toUpperCase();
}

export function DetailsPanel({ frame, onClose }: DetailsPanelProps) {
  if (!frame) {
    return (
      <aside className="details-panel" id="details-panel" aria-label="Frame details">
        <div className="empty-details">
          <svg viewBox="0 0 24 24" width="32" height="32" stroke="currentColor" strokeWidth="1.2" fill="none" style={{ opacity: .3 }}>
            <rect x="3" y="4" width="18" height="16" rx="2"/>
            <path d="M9 4v16M15 8h3M15 12h3M15 16h3"/>
          </svg>
          <div>
            <div style={{ fontWeight: 600, marginBottom: 4 }}>No frame selected</div>
            <div style={{ fontSize: 12 }}>Click a row in the CAN table to inspect its decoded signals</div>
          </div>
        </div>
      </aside>
    );
  }

  const msgName = frame.message_key?.split(':')[1]?.toUpperCase() ?? frame.id.toUpperCase();
  const bus = BUS_NAMES[frame.channel] ?? `ch${frame.channel}`;
  const isStale = frame.age_ms > 2000;

  return (
    <aside className="details-panel" id="details-panel" aria-label="Frame details">
      <div className="details-header">
        <div style={{ minWidth: 0 }}>
          <span className="details-eyebrow">{bus} bus · {frame.id.toUpperCase()}</span>
          <h2 className="details-title" id="details-title">{msgName}</h2>
          {frame.message_key && (
            <span style={{ fontSize: 11, color: 'var(--text-tertiary)', fontFamily: 'ui-monospace,Menlo,Consolas,monospace' }}>
              {frame.message_key}
            </span>
          )}
        </div>
        <button className="details-close" id="details-close" onClick={onClose} title="Close">
          <svg viewBox="0 0 24 24" width="14" height="14" stroke="currentColor" strokeWidth="2" fill="none">
            <path d="m6 6 12 12M18 6 6 18"/>
          </svg>
        </button>
      </div>

      <div className="details-scroll">
        {/* Status badges */}
        <div style={{ display: 'flex', gap: 6, flexWrap: 'wrap' }}>
          {frame.is_error && <span className="status-badge danger">Error Frame</span>}
          {isStale && <span className="status-badge warning">Stale (&gt;2s)</span>}
          {!frame.is_error && !isStale && <span className="status-badge success">Fresh</span>}
          <span className={`bus-badge ${bus}`} style={{ padding: '3px 9px', borderRadius: 999, fontSize: 11.5, fontWeight: 600 }}>{bus}</span>
        </div>

        {/* Frame properties */}
        <div className="detail-section">
          <h3>Frame Info</h3>
          <div className="property-list">
            {[
              ['CAN ID', frame.id.toUpperCase()],
              ['DLC', `${frame.dlc} bytes`],
              ['Raw Data', formatHex(frame.data)],
              ['Frame Count', frame.count.toLocaleString()],
              ['Age', `${frame.age_ms.toFixed(0)} ms`],
              ['Δt', frame.delta_t_ms > 0 ? `${frame.delta_t_ms.toFixed(1)} ms` : '—'],
              ['Decode', frame.decode_status ?? 'unknown'],
            ].map(([k, v]) => (
              <div key={k} className="property-row">
                <span className="property-key">{k}</span>
                <span className="property-val mono">{v}</span>
              </div>
            ))}
          </div>
        </div>

        {/* Decoded signals */}
        {frame.signals && Object.keys(frame.signals).length > 0 && (
          <div className="detail-section">
            <h3>Decoded Signals</h3>
            <div className="signal-list" id="signal-list">
              {Object.entries(frame.signals).map(([name, val]) => (
                <div key={name} className="signal-item" id={`sig-${name}`}>
                  <span className="signal-name">{name}</span>
                  <span className="signal-val">
                    {typeof val === 'number' ? val.toLocaleString(undefined, { maximumFractionDigits: 4 }) : String(val)}
                  </span>
                </div>
              ))}
            </div>
          </div>
        )}

        {/* Raw bytes breakdown */}
        <div className="detail-section">
          <h3>Byte Layout</h3>
          <div style={{ display: 'flex', gap: 4, flexWrap: 'wrap' }}>
            {(frame.data.match(/.{1,2}/g) ?? []).map((byte, i) => (
              <div key={i} style={{
                display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 2,
                padding: '6px 8px', border: '1px solid var(--border)', borderRadius: 4,
                background: 'var(--surface-subtle)', minWidth: 36
              }}>
                <span style={{ fontSize: 9, color: 'var(--text-tertiary)', fontWeight: 600 }}>B{i}</span>
                <span style={{ fontFamily: 'ui-monospace,Menlo,Consolas,monospace', fontSize: 13, fontWeight: 700 }}>
                  {byte.toUpperCase()}
                </span>
                <span style={{ fontSize: 9, color: 'var(--text-tertiary)' }}>{parseInt(byte, 16)}</span>
              </div>
            ))}
          </div>
        </div>
      </div>
    </aside>
  );
}
