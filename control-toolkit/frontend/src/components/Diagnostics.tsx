import { useCallback, useEffect, useMemo, useState } from 'react'
import { api } from '../api'
import { hexId } from '../lib/format'
import { observeEstop } from '../lib/signals'
import { useAppStore } from '../store'
import { WorkspaceShell } from './WorkspaceShell'

function decodeBlockMask(node: string, mask: number): string[] {
  if (!mask || mask === 0) return ['None']
  const reasons: string[] = []
  if (node.toLowerCase() === 'sys') {
    if (mask & (1 << 0)) reasons.push('Physical button pressed')
    if (mask & (1 << 1)) reasons.push('Brake fault')
    if (mask & (1 << 2)) reasons.push('MTR unavailable')
    if (mask & (1 << 3)) reasons.push('RT fault')
    if (mask & (1 << 4)) reasons.push('MTR ESTOP active')
    if (mask & (1 << 5)) reasons.push('Traction fault')
    if (mask & (1 << 6)) reasons.push('Steer fault')
    if (mask & (1 << 7)) reasons.push('Heartbeat loss')
    if (mask & (1 << 8)) reasons.push('CAN bad')
    if (mask & (1 << 9)) reasons.push('Brake pressure low')
    if (mask & (1 << 10)) reasons.push('ESTOP active')
  } else if (node.toLowerCase() === 'rt') {
    if (mask & (1 << 0)) reasons.push('No SYS authority')
    if (mask & (1 << 1)) reasons.push('No Host authority')
    if (mask & (1 << 2)) reasons.push('MTR unavailable')
    if (mask & (1 << 3)) reasons.push('Steer not ready')
  }
  return reasons.length > 0 ? reasons : [`0x${mask.toString(16)}`]
}

export function Diagnostics() {
  const status = useAppStore((s) => s.status)
  const setStatus = useAppStore((s) => s.setStatus)
  const quality = useAppStore((s) => s.streamQuality)
  const mismatch = useAppStore((s) => s.protocolMismatch)
  const messages = useAppStore((s) => s.messages)
  const estopLive = useMemo(
    () => observeEstop(messages, status?.session),
    [messages, status?.session],
  )
  /** Prefer API report (status.estop) when present; fall back to live observe. */
  const estopApi = status?.estop
  const [events, setEvents] = useState<Array<Record<string, unknown>>>([])
  const [episodes, setEpisodes] = useState<Array<Record<string, unknown>>>([])
  const [activeRec, setActiveRec] = useState<Record<string, unknown> | null>(null)
  const [recordings, setRecordings] = useState<Array<Record<string, unknown>>>([])
  const [tests, setTests] = useState<Array<Record<string, unknown>>>([])
  const [recLog, setRecLog] = useState('')
  const [testLog, setTestLog] = useState('')
  const [busy, setBusy] = useState(false)
  const [activeTestId, setActiveTestId] = useState<string | null>(null)
  const [evidenceId, setEvidenceId] = useState<string | null>(null)
  const [evidenceFrames, setEvidenceFrames] = useState<Array<Record<string, unknown>>>(
    [],
  )
  const [evidenceMeta, setEvidenceMeta] = useState('')

  const [diagErr, setDiagErr] = useState('')

  const refreshDiag = useCallback(async () => {
    try {
      const [ev, ep, rec, verification, st] = await Promise.all([
        api.events(40),
        api.episodes(),
        api.recordings(),
        api.tests(),
        api.status(),
      ])
      setEvents(ev.events || [])
      setEpisodes(ep.episodes || [])
      setActiveRec(rec.active && typeof rec.active === 'object' ? rec.active : null)
      setRecordings(rec.recordings || [])
      setTests(verification.tests || [])
      setStatus(st)
      setDiagErr('')
    } catch (e) {
      setDiagErr(String(e))
    }
  }, [setStatus])

  useEffect(() => {
    void refreshDiag()
    const id = window.setInterval(() => void refreshDiag(), 2000)
    return () => window.clearInterval(id)
  }, [refreshDiag])

  async function startRec() {
    setBusy(true)
    try {
      const st = await api.status()
      if (!st.session?.session_id) {
        throw new Error('No active session. Start Computer or connect Real in Settings before recording.')
      }
      const r = await api.startRecording()
      const rid =
        r.recording?.recording_id ??
        (r.recording as { id?: string } | undefined)?.id
      setRecLog(`Started ${String(rid)}`)
      await refreshDiag()
    } catch (e) {
      setRecLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function stopRec() {
    setBusy(true)
    try {
      const id = String(activeRec?.recording_id || '')
      if (!id) {
        setRecLog('No active recording')
        return
      }
      const r = await api.stopRecording(id)
      setRecLog(
        `Stopped ${id} · frames ${String(r.recording.frame_count)} · quality ${String(r.recording.evidence_quality)}`,
      )
      await refreshDiag()
    } catch (e) {
      setRecLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function openEvidence(id: string) {
    setBusy(true)
    try {
      const body = await api.evidence(id, 80)
      setEvidenceId(id)
      setEvidenceFrames(body.frames || [])
      setEvidenceMeta(
        `${body.frame_total} frames · quality ${body.evidence_quality || '—'}`,
      )
    } catch (e) {
      setEvidenceId(id)
      setEvidenceFrames([])
      setEvidenceMeta(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function runVerification() {
    setBusy(true)
    setActiveTestId(null)
    try {
      const st = await api.status()
      if (!st.session?.session_id) {
        throw new Error('No active session. Start Computer or connect Real in Settings first.')
      }
      if (st.session.bench_tx !== 'enabled') {
        throw new Error('Bench TX is disabled. Enable it explicitly before verification.')
      }
      const { cleanupControlStreams } = await import('../lib/cleanup')
      const clean = await cleanupControlStreams('diagnostics_verification', { direct: false })
      const started = await api.startTest({
        name: 'UI zero-speed HostDrive loopback',
        stimulus: {
          type: 'inject',
          bus: 'high',
          key: 'host:host_drive_cmd',
          values: { speed_mmps: 0, yaw_rate_mrad_s: 0, gear: 0 },
        },
        expect: {
          type: 'message_observed',
          bus: 'high',
          can_id: 0x300,
          name: 'HOST_DRIVE_CMD',
          timeout_ms: 1500,
        },
      })
      const id = String(started.test.test_id || '')
      setActiveTestId(id || null)
      setTestLog(`RUNNING ${id}${clean.ok ? '' : ` · ${clean.detail}`}`)
      // Poll until terminal disposition
      const deadline = Date.now() + 8000
      let detail = started.test
      while (Date.now() < deadline) {
        if (id) {
          detail = (await api.test(id)).test
          const d = String(detail.disposition || '')
          setTestLog(`${d.toUpperCase()} ${id} · ${String(detail.detail || '')}`)
          if (d && d !== 'running') break
        }
        await new Promise((r) => window.setTimeout(r, 100))
      }
      setActiveTestId(null)
      await refreshDiag()
    } catch (e) {
      setTestLog(String(e))
      setActiveTestId(null)
    } finally {
      setBusy(false)
    }
  }

  async function cancelVerification() {
    if (!activeTestId) return
    try {
      const r = await api.cancelTest(activeTestId)
      setTestLog(
        `CANCEL ${activeTestId} · ${String(r.test.detail || 'cancel requested')}`,
      )
    } catch (e) {
      setTestLog(String(e))
    }
  }

  return (
    <WorkspaceShell
      testId="workspace-diagnostics"
      className="diagnostics-workspace"
      title="Diagnostics"
      description="Protocol health, verification recipes, and episode capture."
      sectionLabel="Analysis"
    >

      <section className="panel">
        <h2>Session evidence snapshot</h2>
        {diagErr ? (
          <p className="danger-text" data-testid="diagnostics-error">
            {diagErr}
          </p>
        ) : null}
        <dl className="kv">
          <dt>Phase</dt>
          <dd data-testid="diag-phase">{status?.session?.phase ?? '—'}</dd>
          <dt>Stream</dt>
          <dd data-testid="diag-stream">{quality}</dd>
          <dt>Wire hash</dt>
          <dd className="mono">{status?.wire_hash ?? '—'}</dd>
          <dt>Recording</dt>
          <dd data-testid="diag-recording">
            {status?.session?.recording || activeRec ? 'on' : 'off'}
          </dd>
          <dt>Mismatch</dt>
          <dd>{mismatch ? 'yes' : 'no'}</dd>
        </dl>
        <div className="actions tight">
          <button
            type="button"
            className="secondary"
            data-testid="btn-diag-refresh"
            disabled={busy}
            onClick={() => void refreshDiag()}
          >
            Refresh diagnostics
          </button>
        </div>
      </section>

      {/* Root-Cause Diagnostic Hero Card */}
      <section
        className={`diag-root-cause-hero ${estopLive.any || estopApi?.active ? 'hazard' : 'healthy'}`}
        data-testid="diag-estop-panel"
      >
        <div className="diag-hero-header">
          <div className="diag-hero-title">
            <span className={`pulse-indicator ${estopLive.any || estopApi?.active ? 'danger-text' : 'ok-text'}`} />
            <span>
              {estopLive.any || estopApi?.active ? (
                <>
                  <span className="badge-root">Root Cause</span>{' '}
                  {estopApi?.primary_cause ||
                    (estopLive.rtReasonCode !== 0
                      ? `RT: ${estopLive.rtReasonLabel}`
                      : estopLive.causes[0] || 'Active Safety Stop')}
                </>
              ) : (
                <span className="ok-text">Safety Systems Normal · No Active Inhibit</span>
              )}
            </span>
          </div>
          {estopApi?.cause_resolution && (
            <span className="badge badge-subtle mono">
              attribution={estopApi.cause_resolution}
            </span>
          )}
        </div>

        <p className="muted small">
          {estopApi?.summary || estopLive.detail}
        </p>

        <div className="diag-hero-grid">
          {/* Left: Root Cause vs Downstream Cascades */}
          <div className="diag-hero-col">
            <dl className="kv" data-testid="diag-estop-summary">
              <dt>Status</dt>
              <dd className={estopLive.any ? 'danger-text font-semibold' : 'ok-text'}>
                {estopLive.any ? 'STOP / INHIBITED' : 'CLEAR'}
              </dd>
              <dt>Originating Bus</dt>
              <dd className="mono">
                {estopLive.busHigh && estopLive.busLow
                  ? 'High CAN & Low CAN'
                  : estopLive.busHigh
                    ? 'High CAN (Ch0)'
                    : estopLive.busLow
                      ? 'Low CAN (Ch1)'
                      : 'Internal / Interlock'}
              </dd>
              <dt>Host Latch</dt>
              <dd className="mono">{estopLive.hostLatch ? 'LATCHED (Active)' : 'Clear'}</dd>
              <dt>Bus 0x001 Frame</dt>
              <dd className="mono">
                High={estopLive.busHigh ? 'RECENT' : '—'} · Low={estopLive.busLow ? 'RECENT' : '—'}
              </dd>
              <dt>SYS Interlocks</dt>
              <dd className="mono">
                estop={estopLive.sysReported ? '1' : '0'}
                {estopLive.sysHeartbeatBad ? ' · heartbeat_ok=0' : ''}
                {estopLive.sysCanBad ? ' · can_ok=0' : ''}
                {estopLive.sysBrakeFault ? ' · brake_fault' : ''}
              </dd>
              <dt>RT Mode & Reason</dt>
              <dd className="mono" data-testid="diag-estop-rt-reason">
                {estopLive.rtStale ? (
                  <span className="muted">
                    frame stale · last mode={estopLive.rtMode || '—'} · last reason={estopLive.lastKnownReasonCode} ({estopLive.lastKnownReasonLabel})
                  </span>
                ) : (
                  <>
                    mode={estopLive.rtMode || '—'} · reason={estopLive.rtReasonCode}:{' '}
                    {estopApi?.rt?.estop_reason_display || estopLive.rtReasonLabel}
                    {estopLive.safetyState != null ? ` · safety_state=${estopLive.safetyState}` : ''}
                  </>
                )}
              </dd>
            </dl>

            {/* Downstream Cascades List */}
            {(estopApi?.causes?.length || estopLive.causes.length) > 0 ? (
              <div className="mt-2">
                <div className="text-xs font-bold uppercase text-secondary mb-1">
                  Active Sources & Cascade Reactions:
                </div>
                <div className="diag-cascade-list" data-testid="diag-estop-causes">
                  {(estopApi?.causes?.length ? estopApi.causes : estopLive.causes).map((c, i) => (
                    <div key={c} className="diag-cascade-item">
                      <span className={i === 0 ? 'badge-root' : 'badge-cascade'}>
                        {i === 0 ? 'Trigger' : 'Cascade'}
                      </span>
                      <span className="font-mono">{c}</span>
                    </div>
                  ))}
                </div>
              </div>
            ) : null}
          </div>

          {/* Right: Actionable Operator Recovery Guide */}
          <div className="diag-hero-col">
            <div className="diag-recovery-box">
              <div className="diag-recovery-title">Actionable Recovery Checklist</div>
              <ol className="diag-recovery-steps">
                {estopLive.hostLatch && (
                  <li>
                    <strong>Host inject latch is active:</strong> Click &quot;Clear Host Latch&quot; below to clear the testbed software latch.
                  </li>
                )}
                {estopLive.sysBrakeFault && (
                  <li>
                    <strong>SYS Brake Fault:</strong> Verify hydraulic pressure sensor calibration and BBW feedback wiring on Low CAN.
                  </li>
                )}
                {estopLive.sysHeartbeatBad && (
                  <li>
                    <strong>SYS Heartbeat Loss:</strong> Inspect SYS ECU 12V harness and 0x7FE transmission cycle.
                  </li>
                )}
                {estopLive.rtReasonCode === 10 && (
                  <li>
                    <strong>RT Watchdog:</strong> Real-time task loop overrun detected. Check task execution time in RT telemetry.
                  </li>
                )}
                {estopLive.busHigh || estopLive.busLow ? (
                  <li>
                    <strong>0x001 SAFETY_ESTOP:</strong> Physical button or bus broadcast active. Release hardware e-stop button if depressed.
                  </li>
                ) : null}
                {!estopLive.any && (
                  <li>All safety parameters in bounds. System ready to arm Bench TX or transition to Active.</li>
                )}
              </ol>

              {estopLive.hostLatch && (
                <div className="mt-3">
                  <button
                    type="button"
                    className="secondary w-full text-xs font-bold"
                    data-testid="btn-diag-clear-latch"
                    disabled={busy}
                    onClick={async () => {
                      setBusy(true)
                      try {
                        await api.clearEstop()
                        await refreshDiag()
                      } finally {
                        setBusy(false)
                      }
                    }}
                  >
                    Clear Host Latch (Software Only)
                  </button>
                </div>
              )}
            </div>
          </div>
        </div>

        {/* Structured sources breakdown if provided by backend */}
        {estopApi?.sources && Array.isArray(estopApi.sources) && estopApi.sources.length > 0 ? (
          <div className="mt-section">
            <h3>Fault Sources Breakdown</h3>
            <table className="data-table compact" data-testid="diag-estop-sources">
              <thead>
                <tr>
                  <th>Source</th>
                  <th>Detail</th>
                </tr>
              </thead>
              <tbody>
                {estopApi.sources.map((s, i) => (
                  <tr key={String(s.id ?? i)}>
                    <td>{String(s.title ?? s.id ?? '—')}</td>
                    <td className="mono small">{String(s.detail ?? '—')}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        ) : null}

        {/* Node Blockers (NODE_STATUS) */}
        {(() => {
          const estopNodes = (estopApi as { nodes?: Record<string, { state?: string; block_mask?: number }> } | undefined)?.nodes
          if (!estopNodes || Object.keys(estopNodes).length === 0) return null
          return (
            <div className="mt-section">
              <h3>Node Blockers (NODE_STATUS)</h3>
              <table className="data-table compact" data-testid="diag-node-blockers">
                <thead>
                  <tr>
                    <th>Node</th>
                    <th>State</th>
                    <th>Block Mask</th>
                    <th>Decoded Blockers</th>
                  </tr>
                </thead>
                <tbody>
                  {Object.entries(estopNodes).map(([nodeName, nodeState]) => {
                    const ns = nodeState as { state?: string; block_mask?: number }
                    const mask = Number(ns.block_mask ?? 0)
                    return (
                      <tr key={nodeName}>
                        <td className="mono font-semibold uppercase">{nodeName}</td>
                        <td>{ns.state ?? '—'}</td>
                        <td className="mono">{mask} (0x{mask.toString(16)})</td>
                        <td>{decodeBlockMask(nodeName, mask).join(', ')}</td>
                      </tr>
                    )
                  })}
                </tbody>
              </table>
            </div>
          )
        })()}

        {/* RT Diagnostic Events (0x621) */}
        {estopApi?.rt?.diag_events && estopApi.rt.diag_events.length > 0 ? (
          <div className="mt-section">
            <h3>RT Diagnostic Events (0x621)</h3>
            <table className="data-table compact" data-testid="diag-rt-events">
              <thead>
                <tr>
                  <th>Diag ID</th>
                  <th>Event Key</th>
                  <th>State</th>
                  <th>Severity</th>
                  <th>Count</th>
                  <th>Age</th>
                </tr>
              </thead>
              <tbody>
                {estopApi.rt.diag_events.map((ev, i) => (
                  <tr key={String(ev.diag_id ?? i)}>
                    <td className="mono">0x{Number(ev.diag_id ?? 0).toString(16).padStart(4, '0')}</td>
                    <td className="mono font-semibold">{ev.key ?? '—'}</td>
                    <td>
                      <span className={`badge ${ev.state === 'ACTIVE' || ev.state === 'LATCHED' ? 'badge-danger' : 'badge-ok'}`}>
                        {ev.state ?? '—'}
                      </span>
                    </td>
                    <td className="uppercase text-xs">{ev.severity ?? '—'}</td>
                    <td className="mono">{ev.occurrences ?? 1}</td>
                    <td className="mono">{ev.age_ms != null ? `${Math.round(ev.age_ms)} ms` : '—'}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        ) : null}
      </section>

      <section className="panel" data-testid="recording-panel">
        <h2>Recording</h2>
        <p className="muted small">
          Opt-in capture of RX/TX frames. Evidence quality is Complete unless frames are
          dropped. Requires a reachable backend (API on :8001).
        </p>
        <div className="actions">
          <button
            type="button"
            className="primary"
            data-testid="btn-rec-start"
            disabled={busy || !!activeRec || !!diagErr}
            onClick={() => void startRec()}
          >
            Start recording
          </button>
          <button
            type="button"
            className="secondary"
            data-testid="btn-rec-stop"
            disabled={busy || !activeRec}
            onClick={() => void stopRec()}
          >
            Stop recording
          </button>
        </div>
        {activeRec && (
          <dl className="kv">
            <dt>Active ID</dt>
            <dd className="mono">{String(activeRec.recording_id)}</dd>
            <dt>Frames</dt>
            <dd className="mono">{String(activeRec.frame_count)}</dd>
            <dt>Quality</dt>
            <dd>{String(activeRec.evidence_quality)}</dd>
          </dl>
        )}
        {recordings.length > 0 && (
          <div className="mt-section">
            <h3>Recordings</h3>
            <table className="data-table compact" data-testid="recordings-table">
              <thead>
                <tr>
                  <th>ID</th>
                  <th>Frames</th>
                  <th>Quality</th>
                  <th />
                </tr>
              </thead>
              <tbody>
                {recordings.slice(0, 12).map((r) => {
                  const id = String(r.recording_id)
                  return (
                    <tr key={id}>
                      <td className="mono">{id}</td>
                      <td className="num">{String(r.frame_count)}</td>
                      <td>{String(r.evidence_quality)}</td>
                      <td>
                        <div className="actions tight">
                          <button
                            type="button"
                            className="secondary"
                            data-testid={`btn-evidence-${id}`}
                            disabled={busy}
                            onClick={() => void openEvidence(id)}
                          >
                            Open evidence
                          </button>
                          <button
                            type="button"
                            className="secondary"
                            data-testid={`btn-canalyzer-${id}`}
                            disabled={busy || String(r.state) === 'recording'}
                            onClick={() =>
                              window.location.assign(`/api/v1/recordings/${id}/export/vector`)
                            }
                          >
                            Export CANalyzer
                          </button>
                        </div>
                      </td>
                    </tr>
                  )
                })}
              </tbody>
            </table>
          </div>
        )}
        {evidenceId && (
          <div className="mt-section" data-testid="evidence-window">
            <h3>Evidence window · {evidenceId}</h3>
            <p className="muted small">{evidenceMeta}</p>
            <div className="evidence-frames table-wrap">
              <table className="can-table">
                <thead>
                  <tr>
                    <th>Seq</th>
                    <th>Bus</th>
                    <th>ID</th>
                    <th>Dir</th>
                    <th>Data</th>
                  </tr>
                </thead>
                <tbody>
                  {evidenceFrames.map((f) => (
                    <tr key={String(f.seq)}>
                      <td className="mono">{String(f.seq)}</td>
                      <td>{String(f.bus)}</td>
                      <td className="mono">{hexId(Number(f.can_id))}</td>
                      <td>{String(f.direction)}</td>
                      <td className="mono">{String(f.data_hex)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </div>
        )}
        <pre className="log" data-testid="recording-log">
          {recLog || 'Idle.'}
        </pre>
      </section>

      <div className="diagnostics-secondary-grid">
      <section className="panel" data-testid="test-runner-panel">
        <h2>Verification runner</h2>
        <p className="muted small">
          Safe zero-speed High-bus loopback: inject HOST_DRIVE_CMD and verify the decoded
          message is observed. The backend runs one verification step at a time.
        </p>
        <div className="actions tight">
          <button
            type="button"
            className="primary"
            data-testid="btn-run-verification"
            disabled={busy}
            onClick={() => void runVerification()}
          >
            Run zero-speed verification
          </button>
          <button
            type="button"
            className="secondary"
            data-testid="btn-cancel-verification"
            disabled={!activeTestId}
            onClick={() => void cancelVerification()}
          >
            Cancel
          </button>
        </div>
        <pre className="log" data-testid="test-runner-log">
          {testLog || 'No verification run from this UI yet.'}
        </pre>
        {tests.length > 0 && (
          <table className="data-table compact" data-testid="verification-tests-table">
            <thead>
              <tr>
                <th>ID</th>
                <th>Name</th>
                <th>Result</th>
                <th>Duration</th>
              </tr>
            </thead>
            <tbody>
              {tests.slice(0, 8).map((t) => (
                <tr key={String(t.test_id)}>
                  <td className="mono">{String(t.test_id)}</td>
                  <td>{String(t.name)}</td>
                  <td>{String(t.disposition)}</td>
                  <td className="mono">{String(t.duration_ms ?? '—')} ms</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </section>

      <section className="panel" data-testid="episodes-panel">
        <h2>Episodes & Fault Duration</h2>
        <p className="muted small">
          Active fault duration, occurrence count, and recovery lifecycle.
        </p>
        {episodes.length === 0 ? (
          <p className="muted small">No active diagnostic episodes.</p>
        ) : (
          <table className="data-table compact" data-testid="episodes-table">
            <thead>
              <tr>
                <th>Code</th>
                <th>Scope</th>
                <th>Status</th>
                <th>Active Duration</th>
                <th>First Seen</th>
                <th>Count</th>
                <th>Severity</th>
              </tr>
            </thead>
            <tbody>
              {episodes.map((e) => {
                const isRecovered = !!e.recovered
                const durationMs = typeof e.active_duration_ms === 'number' ? e.active_duration_ms : 0
                const durationStr = durationMs > 1000
                  ? `${(durationMs / 1000).toFixed(1)}s`
                  : `${Math.round(durationMs)} ms`
                const firstWall = typeof e.first_wall === 'number' ? e.first_wall : null
                const timeStr = firstWall
                  ? new Date(firstWall * 1000).toLocaleTimeString([], { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' })
                  : '—'
                return (
                  <tr key={String(e.episode_id)}>
                    <td className="mono font-semibold">{String(e.code)}</td>
                    <td className="mono">{String(e.scope)}</td>
                    <td>
                      {isRecovered ? (
                        <span className="badge badge-ok">Recovered</span>
                      ) : (
                        <span className="badge badge-danger">
                          <span className="pulse-indicator inline-block mr-1" />
                          Active
                        </span>
                      )}
                    </td>
                    <td>
                      <span className={isRecovered ? 'episode-timing-recovered' : 'episode-timing-active'}>
                        {durationStr}
                      </span>
                    </td>
                    <td className="mono text-xs">{timeStr}</td>
                    <td className="num mono">
                      {String(e.count)}×
                    </td>
                    <td>
                      <span className={`badge ${String(e.severity) === 'critical' ? 'badge-danger' : 'badge-warning'}`}>
                        {String(e.severity)}
                      </span>
                    </td>
                  </tr>
                )
              })}
            </tbody>
          </table>
        )}
      </section>
      </div>

      <section className="panel" data-testid="events-panel">
        <h2>Event timeline</h2>
        <table className="data-table compact" data-testid="events-table">
          <thead>
            <tr>
              <th>Severity</th>
              <th>Code</th>
              <th>Title</th>
              <th>Cause / evidence</th>
              <th>Age, s</th>
            </tr>
          </thead>
          <tbody>
            {events.map((e) => (
              <tr key={String(e.event_id)}>
                <td>{String(e.severity)}</td>
                <td className="mono">{String(e.code)}</td>
                <td>{String(e.title)}</td>
                <td className="small">{String(e.detail || '—')}</td>
                <td className="num mono">
                  {typeof e.age_s === 'number' ? e.age_s.toFixed(1) : '—'}
                </td>
              </tr>
            ))}
            {events.length === 0 && (
              <tr>
                <td colSpan={5} className="muted">
                  No events yet.
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </section>
    </WorkspaceShell>
  )
}
