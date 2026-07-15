import React, { useState, useMemo } from 'react';
import { useCanStore, type CanFrameState } from '../store/useCanStore';

interface CanTableProps {
  onSelectFrame: (frame: CanFrameState & { channel: string }) => void;
  selectedId: string | null;
}

const BUS_NAMES: Record<string, string> = {
  '0': 'high', '1': 'low'
};

function formatAge(ms: number): string {
  if (ms < 1000) return `${ms.toFixed(0)}ms`;
  return `${(ms / 1000).toFixed(1)}s`;
}

function formatBytes(hex: string): string {
  return hex.replace(/(.{2})/g, '$1 ').trim().toUpperCase();
}

export function CanTable({ onSelectFrame, selectedId }: CanTableProps) {
  const channels = useCanStore(s => s.channels);
  const [search, setSearch] = useState('');

  // Flatten all channels into a single sorted array
  const allFrames = useMemo(() => {
    const rows: (CanFrameState & { channel: string; compositeKey: string })[] = [];
    for (const [ch, frames] of Object.entries(channels)) {
      for (const [id, frame] of Object.entries(frames)) {
        rows.push({ ...frame, channel: ch, compositeKey: `${ch}:${id}` });
      }
    }
    return rows.sort((a, b) => {
      // Sort by channel then by CAN ID numerically
      if (a.channel !== b.channel) return a.channel.localeCompare(b.channel);
      return parseInt(a.id, 16) - parseInt(b.id, 16);
    });
  }, [channels]);

  const filtered = useMemo(() => {
    if (!search.trim()) return allFrames;
    const q = search.toLowerCase();
    return allFrames.filter(f =>
      f.id.toLowerCase().includes(q) ||
      f.message_key?.toLowerCase().includes(q) ||
      f.data.toLowerCase().includes(q) ||
      BUS_NAMES[f.channel]?.includes(q)
    );
  }, [allFrames, search]);

  return (
    <div className="workspace-panel" id="can-table-panel">
      <div className="table-toolbar">
        <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
          <h2 className="panel-title" style={{ margin: 0 }}>CAN Frame Monitor</h2>
          <span className="status-badge info">{filtered.length} IDs</span>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <label className="table-search" htmlFor="can-search">
            <svg viewBox="0 0 24 24" width="14" height="14" stroke="currentColor" strokeWidth="1.8" fill="none">
              <circle cx="11" cy="11" r="7"/><path d="m20 20-4-4"/>
            </svg>
            <input
              id="can-search"
              type="search"
              placeholder="Filter by ID, name, bus…"
              value={search}
              onChange={e => setSearch(e.target.value)}
            />
          </label>
        </div>
      </div>

      <div className="table-wrap">
        <table>
          <thead>
            <tr>
              <th>ID</th>
              <th>Bus</th>
              <th>Message</th>
              <th>DLC</th>
              <th>Data (hex)</th>
              <th>Status</th>
              <th style={{ textAlign: 'right' }}>Age</th>
              <th style={{ textAlign: 'right' }}>Count</th>
              <th style={{ textAlign: 'right' }}>Δt</th>
            </tr>
          </thead>
          <tbody>
            {filtered.length === 0 && (
              <tr>
                <td colSpan={9} style={{ textAlign: 'center', color: 'var(--text-tertiary)', padding: '32px 0' }}>
                  {search ? 'No frames match your filter' : 'Waiting for CAN frames…'}
                </td>
              </tr>
            )}
            {filtered.map(frame => {
              const isSelected = selectedId === frame.compositeKey;
              const isError = frame.is_error;
              const isChanged = frame._just_changed;
              const isStale = frame.age_ms > 2000;

              return (
                <tr
                  key={frame.compositeKey}
                  id={`row-${frame.compositeKey.replace(':', '-')}`}
                  className={[
                    isSelected ? 'selected' : '',
                    isError ? 'error' : '',
                    isChanged && !isSelected ? 'just-changed' : '',
                  ].filter(Boolean).join(' ')}
                  onClick={() => onSelectFrame({ ...frame })}
                >
                  <td>
                    <span className="mono" style={{ fontWeight: 600, color: isError ? 'var(--danger)' : 'var(--text)' }}>
                      {frame.id.toUpperCase()}
                    </span>
                  </td>
                  <td>
                    <span className={`bus-badge ${BUS_NAMES[frame.channel] || 'high'}`}>
                      {BUS_NAMES[frame.channel] ?? `ch${frame.channel}`}
                    </span>
                  </td>
                  <td style={{ color: 'var(--text-secondary)', fontSize: 12 }}>
                    {frame.message_key ?? <span style={{ color: 'var(--text-tertiary)' }}>unknown</span>}
                  </td>
                  <td className="mono" style={{ color: 'var(--text-secondary)' }}>{frame.dlc}</td>
                  <td>
                    <span className="mono" style={{ fontSize: 11.5, letterSpacing: '.04em' }}>
                      {formatBytes(frame.data)}
                    </span>
                  </td>
                  <td>
                    {isError
                      ? <span className="status-badge danger">Error</span>
                      : isStale
                      ? <span className="status-badge warning">Stale</span>
                      : frame.decode_status === 'ok'
                      ? <span className="status-badge success">OK</span>
                      : <span className="status-badge info">{frame.decode_status ?? 'raw'}</span>
                    }
                  </td>
                  <td className="mono" style={{ textAlign: 'right', color: isStale ? 'var(--warning)' : 'var(--text-secondary)', fontSize: 12 }}>
                    {formatAge(frame.age_ms)}
                  </td>
                  <td className="mono" style={{ textAlign: 'right', color: 'var(--text-secondary)', fontSize: 12 }}>
                    {frame.count.toLocaleString()}
                  </td>
                  <td className="mono" style={{ textAlign: 'right', color: 'var(--text-secondary)', fontSize: 12 }}>
                    {frame.delta_t_ms > 0 ? `${frame.delta_t_ms.toFixed(0)}ms` : '—'}
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>

      <div className="table-footer">
        <span>{allFrames.length} unique CAN IDs across {Object.keys(channels).length} channels</span>
        <span style={{ fontSize: 11, color: 'var(--text-tertiary)' }}>Click a row to inspect signals</span>
      </div>
    </div>
  );
}
