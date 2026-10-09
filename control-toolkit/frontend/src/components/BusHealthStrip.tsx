/**
 * Compact Dual-Channel CAN Bus Health & Telemetry Strip.
 * Ergonomically designed to fit directly above the Live CAN search toolbar.
 * Occupies only ~42px height while delivering high-density metrics.
 */

import { memo, useEffect, useState } from 'react'
import { useAppStore } from '../store'
import { busLoadTracker, type BusChannelMetrics } from '../lib/busMetrics'

function StatusDotEl({ tone }: { tone: BusChannelMetrics['tone'] }) {
  const toneMap: Record<BusChannelMetrics['tone'], string> = {
    ok: 'live',
    warn: 'warning',
    danger: 'danger',
    muted: 'muted',
  }
  return <span className={`status-dot ${toneMap[tone] || 'muted'}`} />
}

function LoadMiniBar({ loadPct, peakPct }: { loadPct: number; peakPct: number }) {
  const toneClass = loadPct >= 80 ? 'tone-danger' : loadPct >= 50 ? 'tone-warn' : 'tone-ok'

  return (
    <div className="bus-strip-load" title={`Live Bus Load: ${loadPct}% (Peak: ${peakPct}%)`}>
      <span className="bus-strip-load-label">LOAD</span>
      <div className="bus-strip-load-track">
        <div
          className={`bus-strip-load-fill ${toneClass}`}
          style={{ width: `${Math.max(loadPct, 2)}%` }}
        />
        {peakPct > loadPct && (
          <div
            className="bus-strip-load-peak"
            style={{ left: `${Math.min(peakPct, 98)}%` }}
          />
        )}
      </div>
      <div className="bus-strip-load-nums">
        <span className="bus-strip-load-cur">{loadPct}%</span>
        <span className="bus-strip-load-peak-txt" title="Rolling peak utilization">
          ↑{peakPct}%
        </span>
      </div>
    </div>
  )
}

function ChannelCard({ m }: { m: BusChannelMetrics }) {
  return (
    <div className="bus-strip-card">
      {/* Row 1: Bus Identity, Bitrate, Load bar, Throughput */}
      <div className="bus-strip-row">
        <div className="bus-strip-identity">
          <StatusDotEl tone={m.tone} />
          <span className="bus-strip-name">
            {m.bus === 'high' ? 'HIGH BUS (CH0)' : 'LOW BUS (CH1)'}
          </span>
          <span className="bus-strip-rate-badge">
            {m.bitrateKbps}k
          </span>
        </div>

        <LoadMiniBar loadPct={m.loadPct} peakPct={m.peakPct} />

        <div className="bus-strip-throughput">
          <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
            <path d="M22 12h-4l-3 9L9 3l-3 9H2" />
          </svg>
          <span className="bus-strip-fps">{m.fps}</span>
          <span className="bus-strip-unit">fps</span>
          <span className="bus-strip-sep">·</span>
          <span className="bus-strip-kbps">{m.kbps}</span>
          <span className="bus-strip-unit">kB/s</span>
        </div>
      </div>

      {/* Row 2: Error Tracking, Drops, and Node Roster */}
      <div className="bus-strip-row">
        {/* TEC / REC & Errors */}
        <div className="bus-strip-errors">
          <div
            className={`bus-strip-tec-rec ${m.tec && m.tec > 0 ? 'active-error' : ''}`}
            title="Transmit & Receive Error Counters (from ECU Diagnostics)"
          >
            <svg width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
              <path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z" />
            </svg>
            <span>TEC {m.tec ?? 0}</span>
            <span>REC {m.rec ?? 0}</span>
          </div>

          <span className="bus-strip-sep">|</span>

          {/* Corrupt / Invalid Frames */}
          <div
            className={`bus-strip-invalids ${m.rxInvalidPerSec > 0 ? 'active-error' : ''}`}
            title="Corrupt or Framing Discrepancies"
          >
            <svg width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
              <path d="m21.73 18-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.73-3" />
            </svg>
            <span>{m.rxInvalidPerSec}/s errors</span>
          </div>

          {/* Queue Drops */}
          {m.rxOverflowDrops > 0 && (
            <>
              <span className="bus-strip-sep">|</span>
              <span className="bus-strip-drops" title="Driver / hardware buffer overflow drops">
                □ {m.rxOverflowDrops} drops
              </span>
            </>
          )}
        </div>

        {/* Node Roster */}
        <div>
          <span
            className={`bus-strip-nodes ${m.missingNodes.length === 0 ? 'tone-ok' : 'tone-warn'}`}
            title={
              m.missingNodes.length === 0
                ? `All ${m.expectedNodes} expected ECUs online`
                : `Missing ECUs: ${m.missingNodes.join(', ')}`
            }
          >
            {m.missingNodes.length === 0
              ? `✓ ${m.activeNodes}/${m.expectedNodes} Nodes`
              : `⚠ ${m.activeNodes}/${m.expectedNodes} (${m.missingNodes.join(',')})`}
          </span>
        </div>
      </div>
    </div>
  )
}

export const BusHealthStrip = memo(function BusHealthStrip() {
  const [metrics, setMetrics] = useState(() =>
    busLoadTracker.compute(
      useAppStore.getState().messages,
      useAppStore.getState().status,
    ),
  )

  useEffect(() => {
    // Exact 1-second cadence to refresh bus load without visual jitter or layout twitching
    const timer = setInterval(() => {
      const state = useAppStore.getState()
      setMetrics(busLoadTracker.compute(state.messages, state.status))
    }, 1000)
    return () => clearInterval(timer)
  }, [])

  return (
    <div
      data-testid="live-can-bus-health-strip"
      className="bus-strip-container"
    >
      <ChannelCard m={metrics.high} />
      <ChannelCard m={metrics.low} />
    </div>
  )
})
