/**
 * Live CAN workspace — extracted from App.tsx for maintainability.
 *
 * Styling: Tailwind utilities for layout/structure; App.css keeps complex
 * interactive patterns (can-table selection, seg control geometry lock,
 * freshness pills) until those are migrated in a later phase.
 */
import { useEffect, useMemo, useRef, useState } from 'react'
import { useVirtualizer } from '@tanstack/react-virtual'
import { api } from '../api'
import { formatAge, hexId } from '../lib/format'
import { cn } from '../lib/utils'
import { useAppStore } from '../store'
import { FreshnessBadge } from './FreshnessBadge'
import { Button } from './ui/button'
import { Input } from './ui/input'
import { Seg, SegButton } from './ui/seg'
import { WorkspaceShell } from './WorkspaceShell'

export type HistoryFrame = {
  global_sequence: number
  bus: string
  can_id: number
  dlc: number
  data_hex: string
  direction: string
  source: string
  is_extended?: boolean
}

export function LiveCan() {
  const messages = useAppStore((s) => s.messages)
  const liveFilter = useAppStore((s) => s.liveFilter)
  const setLiveFilter = useAppStore((s) => s.setLiveFilter)
  const selected = useAppStore((s) => s.selectedMessageKey)
  const setSelected = useAppStore((s) => s.setSelectedMessageKey)
  const [busFilter, setBusFilter] = useState<'both' | 'high' | 'low'>('both')
  const [viewMode, setViewMode] = useState<'latest' | 'chrono'>('latest')
  const [paused, setPaused] = useState(false)
  const [chrono, setChrono] = useState<HistoryFrame[]>([])
  const [chronoFrozen, setChronoFrozen] = useState<HistoryFrame[]>([])
  const [chronoDetail, setChronoDetail] = useState<{
    frame: HistoryFrame
    decoded: Awaited<ReturnType<typeof api.decodeFrame>>
  } | null>(null)

  const parentRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    if (viewMode !== 'chrono' || paused) return
    let cancelled = false
    async function poll() {
      try {
        const r = await api.history(300)
        if (!cancelled) setChrono(r.frames || [])
      } catch {
        /* ignore */
      }
    }
    void poll()
    const id = window.setInterval(() => void poll(), 500)
    return () => {
      cancelled = true
      window.clearInterval(id)
    }
  }, [viewMode, paused])

  const filtered = useMemo(() => {
    const q = liveFilter.trim().toLowerCase()
    return [...messages]
      .filter((m) => (busFilter === 'both' ? true : m.bus === busFilter))
      .filter((m) => {
        if (!q) return true
        const id = hexId(m.can_id).toLowerCase()
        const name = (m.name || '').toLowerCase()
        const sigs = Object.keys(m.signals || {}).join(' ').toLowerCase()
        return id.includes(q) || name.includes(q) || sigs.includes(q) || m.bus.includes(q)
      })
      .sort((a, b) => a.bus.localeCompare(b.bus) || a.can_id - b.can_id)
  }, [messages, liveFilter, busFilter])

  const chronoView = paused ? chronoFrozen : chrono
  const chronoFiltered = useMemo(() => {
    const q = liveFilter.trim().toLowerCase()
    return chronoView
      .filter((f) => (busFilter === 'both' ? true : f.bus === busFilter))
      .filter((f) => {
        if (!q) return true
        return (
          hexId(f.can_id).toLowerCase().includes(q) ||
          f.bus.includes(q) ||
          f.data_hex.includes(q) ||
          f.source.toLowerCase().includes(q)
        )
      })
      .slice()
      .reverse()
  }, [chronoView, liveFilter, busFilter])

  const rowVirtualizer = useVirtualizer({
    count: chronoFiltered.length,
    getScrollElement: () => parentRef.current,
    estimateSize: () => 35,
    overscan: 10,
  })
  const virtualItems = rowVirtualizer.getVirtualItems()
  const paddingTop = virtualItems.length > 0 ? virtualItems[0].start : 0
  const paddingBottom =
    virtualItems.length > 0
      ? rowVirtualizer.getTotalSize() - virtualItems[virtualItems.length - 1].end
      : 0

  const detail = filtered.find(
    (m) => `${m.bus}-${m.can_id}` === selected || m.key === selected,
  )

  async function selectChronoFrame(frame: HistoryFrame) {
    try {
      const decoded = await api.decodeFrame({
        bus: frame.bus,
        can_id: frame.can_id,
        data_hex: frame.data_hex,
        is_extended: frame.is_extended,
      })
      setChronoDetail({ frame, decoded })
    } catch {
      setChronoDetail({
        frame,
        decoded: {
          known: false,
          status: 'decode_error',
          bus: frame.bus,
          can_id: frame.can_id,
          data_hex: frame.data_hex,
          signals: null,
        },
      })
    }
  }

  return (
    <WorkspaceShell
      testId="workspace-live"
      className="live-layout max-w-none"
      title="Live CAN"
      description={
        viewMode === 'latest'
          ? `Latest-by-message · updates in place · ${filtered.length} rows`
          : `Chronological stream · ${chronoFiltered.length} frames (pause freezes rendering, not capture)`
      }
    >

      <div className="toolbar flex min-w-0 flex-wrap items-center gap-2.5">
        <Input
          search
          data-testid="live-filter"
          placeholder="Filter ID, name, signal…"
          value={liveFilter}
          onChange={(e) => setLiveFilter(e.target.value)}
        />
        <Seg>
          {(['both', 'high', 'low'] as const).map((b) => (
            <SegButton
              key={b}
              active={busFilter === b}
              data-testid={`filter-bus-${b}`}
              onClick={() => setBusFilter(b)}
            >
              {b === 'both' ? 'Both buses' : b}
            </SegButton>
          ))}
        </Seg>
        <Seg data-testid="live-view-mode">
          <SegButton
            active={viewMode === 'latest'}
            data-testid="live-mode-latest"
            onClick={() => setViewMode('latest')}
          >
            Latest
          </SegButton>
          <SegButton
            active={viewMode === 'chrono'}
            data-testid="live-mode-chrono"
            onClick={() => setViewMode('chrono')}
          >
            Stream
          </SegButton>
        </Seg>
        {viewMode === 'chrono' && (
          <Button
            variant="secondary"
            size="dense"
            data-testid="live-pause"
            onClick={() => {
              if (!paused) setChronoFrozen(chrono)
              setPaused((p) => !p)
            }}
          >
            {paused ? 'Resume' : 'Pause'}
          </Button>
        )}
      </div>

      <div
        className={cn(
          'live-split grid items-start gap-3.5',
          'grid-cols-1 min-[1451px]:grid-cols-[1fr_minmax(280px,340px)]',
        )}
      >
        <div
          className={cn(
            'table-wrap max-h-[calc(100vh-220px)] overflow-auto',
            'rounded-[var(--radius)] border border-[var(--border)] bg-[var(--surface)]',
          )}
          ref={parentRef}
        >
          {viewMode === 'latest' ? (
            <table className="can-table" data-testid="live-can-table">
              <thead>
                <tr>
                  <th>Fresh</th>
                  <th>Bus</th>
                  <th>ID</th>
                  <th>Name</th>
                  <th>Rate, Hz</th>
                  <th>Valid</th>
                  <th>Age</th>
                  <th>Signals</th>
                </tr>
              </thead>
              <tbody>
                {filtered.map((m) => {
                  // Prefer bus+can_id — canonical keys like safety:safety_estop collide on high+low
                  const key = `${m.bus}-${m.can_id}`
                  return (
                    <tr
                      key={key}
                      data-testid={`row-${m.bus}-${m.can_id}`}
                      className={selected === key || selected === m.key ? 'selected' : undefined}
                      onClick={() => setSelected(key)}
                    >
                      <td>
                        <FreshnessBadge value={m.freshness} />
                      </td>
                      <td>{m.bus}</td>
                      <td className="mono">{hexId(m.can_id)}</td>
                      <td>{m.name}</td>
                      <td className="num mono">
                        {m.observed_rate_hz != null ? m.observed_rate_hz.toFixed(1) : '—'}
                        {m.expected_rate_hz != null ? ` / ${m.expected_rate_hz}` : ''}
                      </td>
                      <td>{m.validation_status}</td>
                      <td className="mono muted age-cell min-w-[72px] whitespace-nowrap">
                        {formatAge(m.age_ms)}
                      </td>
                      <td className="signals-cell">{Object.entries(m.signals || {})
                          .map(([k, v]) => `${k}=${v.enum_label ?? v.engineering_value}`)
                          .join(' · ')}
                      </td>
                    </tr>
                  )
                })}
                {filtered.length === 0 && (
                  <tr>
                    <td colSpan={8} className="muted">
                      No frames yet — open Control, enable Bench TX, inject host drive.
                    </td>
                  </tr>
                )}
              </tbody>
            </table>
          ) : (
            <table className="can-table" data-testid="live-chrono-table">
              <thead>
                <tr>
                  <th>Seq</th>
                  <th>Bus</th>
                  <th>ID</th>
                  <th>Dir</th>
                  <th>Src</th>
                  <th>DLC</th>
                  <th>Data</th>
                </tr>
              </thead>
              <tbody>
                {paddingTop > 0 && (
                  <tr>
                    <td style={{ height: paddingTop, padding: 0, border: 0 }} colSpan={7} />
                  </tr>
                )}
                {virtualItems.map((virtualRow) => {
                  const f = chronoFiltered[virtualRow.index]
                  return (
                    <tr
                      key={virtualRow.key}
                      data-index={virtualRow.index}
                      ref={rowVirtualizer.measureElement}
                      className={
                        chronoDetail?.frame.global_sequence === f.global_sequence
                          ? 'selected'
                          : undefined
                      }
                      data-testid={`chrono-row-${f.global_sequence}`}
                      onClick={() => void selectChronoFrame(f)}
                    >
                      <td className="mono num">{f.global_sequence}</td>
                      <td>{f.bus}</td>
                      <td className="mono">{hexId(f.can_id)}</td>
                      <td>{f.direction}</td>
                      <td>{f.source}</td>
                      <td className="num">{f.dlc}</td>
                      <td className="mono signals-cell">{f.data_hex}</td>
                    </tr>
                  )
                })}
                {paddingBottom > 0 && (
                  <tr>
                    <td style={{ height: paddingBottom, padding: 0, border: 0 }} colSpan={7} />
                  </tr>
                )}
                {chronoFiltered.length === 0 && (
                  <tr>
                    <td colSpan={7} className="muted">
                      No history frames yet.
                    </td>
                  </tr>
                )}
              </tbody>
            </table>
          )}
        </div>

        <aside
          className={cn(
            'detail-drawer sticky top-3 max-h-[calc(100vh-160px)] overflow-auto',
            'rounded-[var(--radius)] border border-[var(--border)] bg-[var(--surface)]',
            'px-4 py-3.5 max-[1450px]:static max-[1450px]:max-h-none',
          )}
          data-testid="live-detail"
        >
          <h2 className="mt-0">Message detail</h2>
          {viewMode === 'chrono' && !chronoDetail && (
            <p className="muted">Select a historical frame to decode its exact payload.</p>
          )}
          {viewMode === 'chrono' && chronoDetail && (
            <div data-testid="chrono-detail">
              <dl className="kv">
                <dt>Sequence</dt>
                <dd className="mono">{chronoDetail.frame.global_sequence}</dd>
                <dt>Identity</dt>
                <dd className="mono">
                  {chronoDetail.frame.bus} {hexId(chronoDetail.frame.can_id)} ·{' '}
                  {chronoDetail.decoded.name || 'unknown'}
                </dd>
                <dt>Decode</dt>
                <dd>{chronoDetail.decoded.status}</dd>
                <dt>Payload</dt>
                <dd className="mono">{chronoDetail.frame.data_hex || 'DLC 0'}</dd>
              </dl>
              <h3 className="mt-3.5">Signals at this frame</h3>
              <pre className="log chrono-signals">
                {chronoDetail.decoded.signals
                  ? JSON.stringify(chronoDetail.decoded.signals, null, 2)
                  : 'No generated decoder for this identity.'}
              </pre>
            </div>
          )}
          {viewMode === 'latest' && !detail && (
            <p className="muted">Select a row to inspect identity, health, and signals.</p>
          )}
          {viewMode === 'latest' && detail && (
            <>
              <dl className="kv">
                <dt>Identity</dt>
                <dd className="mono">
                  {detail.bus} {hexId(detail.can_id)} · {detail.name}
                </dd>
                <dt>Freshness</dt>
                <dd>
                  <FreshnessBadge value={detail.freshness} />
                </dd>
                <dt>Validation</dt>
                <dd>{detail.validation_status}</dd>
                <dt>Observed rate</dt>
                <dd className="mono">
                  {detail.observed_rate_hz?.toFixed(2) ?? '—'} Hz
                  {detail.expected_rate_hz != null
                    ? ` (expected ${detail.expected_rate_hz})`
                    : ''}
                </dd>
                <dt>Last seen</dt>
                <dd className="mono">{formatAge(detail.age_ms)} ago</dd>
              </dl>
              <h3 className="mt-3.5">Signals</h3>
              <table className="data-table compact">
                <thead>
                  <tr>
                    <th>Signal</th>
                    <th>Value</th>
                    <th>Raw</th>
                    <th>Unit</th>
                  </tr>
                </thead>
                <tbody>
                  {Object.entries(detail.signals || {}).map(([k, v]) => (
                    <tr key={k}>
                      <td>{k}</td>
                      <td className="mono">
                        {String(v.enum_label ?? v.engineering_value ?? '—')}
                      </td>
                      <td className="mono muted">{v.raw_value ?? '—'}</td>
                      <td className="muted">{v.unit ?? ''}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </>
          )}
        </aside>
      </div>
    </WorkspaceShell>
  )
}
