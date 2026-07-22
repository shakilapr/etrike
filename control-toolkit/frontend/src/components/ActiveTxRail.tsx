/**
 * Active TX rail — host-side CAN output (inject / control / analysis).
 * Always open on Inject + Control. Other workspaces: collapsed strip, optional expand.
 * Visual: blue (host TX), not green (ECU RX live).
 */
import { useEffect, useMemo, useState } from 'react'
import {
  hostTxKeySet,
  jobCanIdText,
  jobLabel,
  type ActiveJob,
  type PausedJob,
} from '../lib/activeTx'
import { useActiveTxStore } from '../lib/activeTxStore'
import { useAppStore, type Workspace } from '../store'
import { BusChip, StatusDot } from './ui'

/** Inject / Control open by default; every workspace can collapse or expand. */
const DEFAULT_OPEN: ReadonlySet<Workspace> = new Set(['inject', 'control'])

export function ActiveTxRail() {
  const workspace = useAppStore((s) => s.workspace)
  const jobs = useActiveTxStore((s) => s.jobs)
  const paused = useActiveTxStore((s) => s.paused)
  const catalog = useActiveTxStore((s) => s.catalog)
  const expandedId = useActiveTxStore((s) => s.expandedId)
  const busy = useActiveTxStore((s) => s.busy)
  const note = useActiveTxStore((s) => s.note)
  const setExpandedId = useActiveTxStore((s) => s.setExpandedId)
  const refreshJobs = useActiveTxStore((s) => s.refreshJobs)
  const loadCatalog = useActiveTxStore((s) => s.loadCatalog)
  const pauseJob = useActiveTxStore((s) => s.pauseJob)
  const playPaused = useActiveTxStore((s) => s.playPaused)
  const removeRunning = useActiveTxStore((s) => s.removeRunning)
  const removePaused = useActiveTxStore((s) => s.removePaused)
  const stopAll = useActiveTxStore((s) => s.stopAll)

  const primary = DEFAULT_OPEN.has(workspace)
  /** User-toggled open state; reset to workspace default on tab change. */
  const [open, setOpen] = useState(() => DEFAULT_OPEN.has(workspace))

  useEffect(() => {
    void loadCatalog()
    void refreshJobs()
    const timer = window.setInterval(() => void refreshJobs(), 2000)
    return () => window.clearInterval(timer)
  }, [loadCatalog, refreshJobs])

  // Workspace change → Inject/Control open by default; others collapsed.
  useEffect(() => {
    setOpen(DEFAULT_OPEN.has(workspace))
  }, [workspace])

  const railCount = jobs.length + paused.length

  // Collapsed strip — still polls jobs for host-TX coloring.
  if (!open) {
    return (
      <aside
        className={`active-tx-rail inject-rail is-collapsed${primary ? ' is-primary' : ' is-optional'}`}
        data-testid="inject-side-manager"
        data-rail-open="0"
        aria-label="Active host TX (collapsed)"
      >
        <button
          type="button"
          className="active-tx-collapsed-btn"
          data-testid="active-tx-expand"
          title={
            railCount > 0
              ? `Expand Active TX · ${railCount} job(s)`
              : 'Expand Active TX'
          }
          aria-expanded={false}
          onClick={() => setOpen(true)}
        >
          <StatusDot
            tone={railCount > 0 ? 'tx' : 'muted'}
            title={railCount > 0 ? 'Host TX active' : 'No host TX'}
          />
          <span className="active-tx-collapsed-label">TX</span>
          <span className="mono active-tx-collapsed-count" data-testid="inject-active-count">
            {railCount}
          </span>
        </button>
      </aside>
    )
  }

  return (
    <aside
      className={`active-tx-rail inject-rail${primary ? ' is-primary' : ' is-optional'}`}
      data-testid="inject-side-manager"
      data-rail-open="1"
      aria-label="Active host TX"
    >
      <div className="inject-rail-head" data-testid="inject-active-jobs">
        <div className="inject-rail-title">
          <strong>Active TX</strong>
          <span className="mono muted small" data-testid="inject-active-count">
            {railCount}
          </span>
        </div>
        <div className="inject-rail-head-actions">
          {railCount > 0 && (
            <button
              type="button"
              className="inject-icon-btn"
              disabled={busy}
              data-testid="inject-stop-all"
              title="Remove all TX"
              aria-label="Remove all TX"
              onClick={() => void stopAll()}
            >
              ×
            </button>
          )}
          <button
            type="button"
            className="inject-icon-btn"
            data-testid="active-tx-collapse"
            title="Collapse Active TX"
            aria-label="Collapse Active TX"
            aria-expanded={true}
            onClick={() => setOpen(false)}
          >
            ›
          </button>
        </div>
      </div>

      {railCount === 0 ? (
        <p className="inject-rail-empty muted small">No host TX</p>
      ) : (
        <div className="inject-active-list" data-testid="inject-active-list">
          {jobs.map((job) => (
            <TxRow
              key={job.job_id}
              job={job}
              catalog={catalog}
              open={expandedId === job.job_id}
              busy={busy}
              onToggle={() =>
                setExpandedId(expandedId === job.job_id ? null : job.job_id)
              }
              onPause={() => void pauseJob(job)}
              onRemove={() => void removeRunning(job.job_id)}
            />
          ))}
          {paused.map((p) => (
            <PausedRow
              key={p.pause_id}
              p={p}
              catalog={catalog}
              open={expandedId === p.pause_id}
              busy={busy}
              onToggle={() =>
                setExpandedId(expandedId === p.pause_id ? null : p.pause_id)
              }
              onPlay={() => void playPaused(p)}
              onRemove={() => removePaused(p.pause_id)}
            />
          ))}
        </div>
      )}

      {note ? (
        <p className="muted small mono inject-rail-note" title={note}>
          {note}
        </p>
      ) : null}
    </aside>
  )
}

/** Hook for monitors: set of bus:can_id currently host-TX. */
export function useHostTxKeys(): Set<string> {
  const jobs = useActiveTxStore((s) => s.jobs)
  const catalog = useActiveTxStore((s) => s.catalog)
  return useMemo(() => hostTxKeySet(jobs, catalog), [jobs, catalog])
}

function TxRow({
  job,
  catalog,
  open,
  busy,
  onToggle,
  onPause,
  onRemove,
}: {
  job: ActiveJob
  catalog: Parameters<typeof jobLabel>[1]
  open: boolean
  busy: boolean
  onToggle: () => void
  onPause: () => void
  onRemove: () => void
}) {
  const idText = jobCanIdText(job, catalog)
  const name = jobLabel(job, catalog)
  const valueEntries = Object.entries(job.values || {})
  return (
    <div
      className={`inject-tx-item is-host-tx is-live${open ? ' is-open' : ''}`}
      data-testid={`inject-active-row-${job.job_id}`}
    >
      <div className="inject-tx-row">
        <button
          type="button"
          className="inject-tx-main"
          aria-expanded={open}
          title={`${name} · host TX every ${job.period_ms} ms — expand payload`}
          onClick={onToggle}
        >
          <StatusDot tone="tx" title="Host TX (our output)" />
          <BusChip bus={job.bus} />
          <span className="mono inject-tx-id">{idText}</span>
          <span className="inject-tx-name" title={name}>
            {name}
          </span>
          <span className="mono inject-tx-rate" title={`Period ${job.period_ms} ms`}>
            {job.period_ms}ms
          </span>
          <span className="inject-tx-chevron muted" aria-hidden>
            {open ? '▾' : '▸'}
          </span>
        </button>
        <button
          type="button"
          className="inject-icon-btn"
          disabled={busy}
          data-testid={`inject-active-pause-${job.job_id}`}
          title="Pause"
          aria-label={`Pause ${name}`}
          onClick={onPause}
        >
          ⏸
        </button>
        <button
          type="button"
          className="inject-icon-btn inject-icon-remove"
          disabled={busy}
          data-testid={`inject-active-stop-${job.job_id}`}
          title="Remove"
          aria-label={`Remove ${name}`}
          onClick={onRemove}
        >
          ×
        </button>
      </div>
      {open && (
        <dl className="inject-tx-stats">
          <div>
            <dt>Owner</dt>
            <dd className="mono">{job.owner || '—'}</dd>
          </div>
          <div>
            <dt>Bus</dt>
            <dd className="mono">{job.bus}</dd>
          </div>
          <div>
            <dt>Period</dt>
            <dd className="mono">{job.period_ms} ms</dd>
          </div>
          <div>
            <dt>Missed</dt>
            <dd className="mono">{job.missed}</dd>
          </div>
          {valueEntries.length === 0 ? (
            <div className="inject-tx-payload-empty">
              <dt>Payload</dt>
              <dd className="muted">No signals (DLC 0 / empty)</dd>
            </div>
          ) : null}
        </dl>
      )}
      {open && valueEntries.length > 0 && (
        <div className="inject-tx-payload">
          <span className="inject-tx-payload-label">Sending</span>
          <ul>
            {valueEntries.map(([k, v]) => (
              <li key={k}>
                <span className="mono inject-tx-key">{k}</span>
                <span className="mono inject-tx-val">{String(v ?? '—')}</span>
              </li>
            ))}
          </ul>
        </div>
      )}
    </div>
  )
}

function PausedRow({
  p,
  catalog,
  open,
  busy,
  onToggle,
  onPlay,
  onRemove,
}: {
  p: PausedJob
  catalog: Parameters<typeof jobLabel>[1]
  open: boolean
  busy: boolean
  onToggle: () => void
  onPlay: () => void
  onRemove: () => void
}) {
  const idText = jobCanIdText(p, catalog)
  const valueEntries = Object.entries(p.values || {})
  return (
    <div
      className={`inject-tx-item is-host-tx is-paused${open ? ' is-open' : ''}`}
      data-testid={`inject-paused-row-${p.pause_id}`}
    >
      <div className="inject-tx-row">
        <button
          type="button"
          className="inject-tx-main"
          aria-expanded={open}
          title={`${p.name} · paused · would send every ${p.period_ms} ms`}
          onClick={onToggle}
        >
          <StatusDot tone="warning" title="Paused host TX" />
          <BusChip bus={p.bus} />
          <span className="mono inject-tx-id">{idText}</span>
          <span className="inject-tx-name" title={p.name}>
            {p.name}
          </span>
          <span className="mono inject-tx-rate" title={`Period ${p.period_ms} ms`}>
            {p.period_ms}ms
          </span>
          <span className="inject-tx-chevron muted" aria-hidden>
            {open ? '▾' : '▸'}
          </span>
        </button>
        <button
          type="button"
          className="inject-icon-btn"
          disabled={busy}
          data-testid={`inject-active-play-${p.pause_id}`}
          title="Play"
          aria-label={`Play ${p.name}`}
          onClick={onPlay}
        >
          ▶
        </button>
        <button
          type="button"
          className="inject-icon-btn inject-icon-remove"
          disabled={busy}
          data-testid={`inject-paused-remove-${p.pause_id}`}
          title="Remove"
          aria-label={`Remove ${p.name}`}
          onClick={onRemove}
        >
          ×
        </button>
      </div>
      {open && (
        <dl className="inject-tx-stats">
          <div>
            <dt>Bus</dt>
            <dd className="mono">{p.bus}</dd>
          </div>
          <div>
            <dt>Period</dt>
            <dd className="mono">{p.period_ms} ms</dd>
          </div>
        </dl>
      )}
      {open && valueEntries.length > 0 && (
        <div className="inject-tx-payload">
          <span className="inject-tx-payload-label">Would send</span>
          <ul>
            {valueEntries.map(([k, v]) => (
              <li key={k}>
                <span className="mono inject-tx-key">{k}</span>
                <span className="mono inject-tx-val">{String(v ?? '—')}</span>
              </li>
            ))}
          </ul>
        </div>
      )}
    </div>
  )
}
