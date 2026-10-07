/**
 * Compact Dual-Channel CAN Bus Health & Telemetry Strip.
 * Ergonomically designed to fit directly above the Live CAN search toolbar.
 * Occupies only ~42px height while delivering high-density metrics.
 */

import { memo, useEffect, useState } from 'react'
import { useAppStore } from '../store'
import { busLoadTracker, type BusChannelMetrics, type BusMetricsSnapshot } from '../lib/busMetrics'

function StatusDot({ tone }: { tone: BusChannelMetrics['tone'] }) {
  const colorMap = {
    ok: 'bg-emerald-500 shadow-[0_0_6px_rgba(16,185,129,0.6)]',
    warn: 'bg-amber-500 shadow-[0_0_6px_rgba(245,158,11,0.6)]',
    danger: 'bg-rose-500 shadow-[0_0_6px_rgba(244,63,94,0.6)] animate-pulse',
    muted: 'bg-slate-400 dark:bg-slate-600',
  }
  return <span className={`inline-block w-2 h-2 rounded-full flex-shrink-0 ${colorMap[tone]}`} />
}

function LoadMiniBar({ loadPct, peakPct }: { loadPct: number; peakPct: number }) {
  const getBarColor = (pct: number) => {
    if (pct >= 80) return 'bg-rose-500'
    if (pct >= 50) return 'bg-amber-500'
    return 'bg-emerald-500'
  }

  return (
    <div className="flex items-center gap-1.5 flex-shrink-0" title={`Live Bus Load: ${loadPct}% (Peak: ${peakPct}%)`}>
      <span className="text-[10px] uppercase font-bold tracking-wider text-slate-500 dark:text-slate-400">LOAD</span>
      <div className="relative w-16 h-2 bg-slate-200 dark:bg-slate-700/70 rounded-sm overflow-hidden">
        {/* Fill */}
        <div
          className={`h-full transition-all duration-300 ${getBarColor(loadPct)}`}
          style={{ width: `${Math.max(loadPct, 2)}%` }}
        />
        {/* Peak indicator */}
        {peakPct > loadPct && (
          <div
            className="absolute top-0 bottom-0 w-0.5 bg-rose-400 opacity-80"
            style={{ left: `${Math.min(peakPct, 98)}%` }}
          />
        )}
      </div>
      <div className="flex items-baseline gap-1 font-mono text-[11px] font-semibold text-slate-800 dark:text-slate-200">
        <span>{loadPct}%</span>
        <span className="text-[9.5px] font-normal text-slate-400 dark:text-slate-500" title="Rolling peak utilization">
          ↑{peakPct}%
        </span>
      </div>
    </div>
  )
}

function ChannelCard({ m }: { m: BusChannelMetrics }) {
  return (
    <div className="flex flex-col justify-between py-1.5 px-3 border-r border-slate-200 dark:border-slate-800 last:border-r-0 min-w-0">
      {/* Row 1: Bus Identity, Bitrate, Load bar, Throughput */}
      <div className="flex items-center justify-between gap-2.5 text-xs">
        <div className="flex items-center gap-1.5 flex-shrink-0">
          <StatusDot tone={m.tone} />
          <span className="font-bold text-[11px] tracking-wide text-slate-900 dark:text-slate-100">
            {m.bus === 'high' ? 'HIGH BUS (CH0)' : 'LOW BUS (CH1)'}
          </span>
          <span className="text-[10px] font-mono px-1 py-0.5 bg-slate-100 dark:bg-slate-800 text-slate-500 dark:text-slate-400 rounded border border-slate-200 dark:border-slate-700/60 leading-none">
            {m.bitrateKbps}k
          </span>
        </div>

        <LoadMiniBar loadPct={m.loadPct} peakPct={m.peakPct} />

        <div className="flex items-center gap-1 text-[11px] font-mono text-slate-700 dark:text-slate-200 flex-shrink-0">
          <svg className="w-3 h-3 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
            <path d="M22 12h-4l-3 9L9 3l-3 9H2" />
          </svg>
          <span className="font-bold">{m.fps}</span>
          <span className="text-[10px] text-slate-400">fps</span>
          <span className="text-slate-300 dark:text-slate-700 font-normal">·</span>
          <span className="font-medium">{m.kbps}</span>
          <span className="text-[10px] text-slate-400">kB/s</span>
        </div>
      </div>

      {/* Row 2: Error Tracking, Drops, and Node Roster */}
      <div className="flex items-center justify-between gap-2 text-[10.5px] text-slate-500 dark:text-slate-400 mt-1 pt-0.5 border-t border-slate-100 dark:border-slate-800/40">
        {/* TEC / REC & Errors */}
        <div className="flex items-center gap-2 font-mono">
          <div className="flex items-center gap-1" title="Transmit & Receive Error Counters (from ECU Diagnostics)">
            <svg className="w-2.5 h-2.5 text-amber-500" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5">
              <path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z" />
            </svg>
            <span className={m.tec && m.tec > 0 ? 'text-amber-600 dark:text-amber-400 font-bold' : 'text-slate-600 dark:text-slate-300'}>
              T:{m.tec ?? 0}
            </span>
            <span className={m.rec && m.rec > 0 ? 'text-amber-600 dark:text-amber-400 font-bold' : 'text-slate-600 dark:text-slate-300'}>
              R:{m.rec ?? 0}
            </span>
          </div>

          <span className="text-slate-200 dark:text-slate-700">|</span>

          {/* Corrupt / Invalid Frames */}
          <div className="flex items-center gap-1" title="Corrupt or Framing Discrepancies">
            <span className={m.rxInvalidPerSec > 0 ? 'text-rose-600 dark:text-rose-400 font-bold' : 'text-slate-500 dark:text-slate-400'}>
              ⚠ {m.rxInvalidPerSec}/s
            </span>
          </div>

          {/* Queue Drops */}
          {m.rxOverflowDrops > 0 && (
            <>
              <span className="text-slate-200 dark:text-slate-700">|</span>
              <span className="text-rose-600 dark:text-rose-400 font-bold" title="Driver / hardware buffer overflow drops">
                □ {m.rxOverflowDrops} drops
              </span>
            </>
          )}
        </div>

        {/* Node Roster */}
        <div className="flex items-center gap-1">
          <span
            className={`px-1.5 py-0.5 rounded text-[10px] font-mono font-semibold leading-tight ${
              m.missingNodes.length === 0
                ? 'bg-emerald-50 dark:bg-emerald-950/40 text-emerald-700 dark:text-emerald-300 border border-emerald-200 dark:border-emerald-800/40'
                : 'bg-amber-50 dark:bg-amber-950/40 text-amber-700 dark:text-amber-300 border border-amber-200 dark:border-amber-800/40'
            }`}
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
  const messages = useAppStore((s) => s.messages)
  const status = useAppStore((s) => s.status)

  // Compute live metrics directly from current store state
  const [tick, setTick] = useState(0)

  useEffect(() => {
    // 1-second cadence to keep rolling peak / window metrics advancing
    const timer = setInterval(() => {
      setTick((t) => t + 1)
    }, 1000)
    return () => clearInterval(timer)
  }, [])

  const metrics = busLoadTracker.compute(messages, status)

  return (
    <div
      data-testid="live-can-bus-health-strip"
      className="grid grid-cols-1 md:grid-cols-2 bg-slate-50 dark:bg-slate-900/60 border border-slate-200 dark:border-slate-800 rounded-md mb-2.5 shadow-sm overflow-hidden"
      style={{ minHeight: '44px' }}
    >
      <ChannelCard m={metrics.high} />
      <ChannelCard m={metrics.low} />
    </div>
  )
})
