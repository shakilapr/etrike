import { useCallback, useEffect, useState } from 'react'
import { api } from '../api'
import { hexId } from '../lib/format'
import { useAppStore } from '../store'
import { WorkspaceShell } from './WorkspaceShell'

export function Diagnostics() {
  const status = useAppStore((s) => s.status)
  const setStatus = useAppStore((s) => s.setStatus)
  const quality = useAppStore((s) => s.streamQuality)
  const mismatch = useAppStore((s) => s.protocolMismatch)
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
        <h2>Episodes</h2>
        {episodes.length === 0 ? (
          <p className="muted small">No active diagnostic episodes.</p>
        ) : (
          <table className="data-table compact">
            <thead>
              <tr>
                <th>Code</th>
                <th>Scope</th>
                <th>Count</th>
                <th>Severity</th>
                <th>Recovered</th>
              </tr>
            </thead>
            <tbody>
              {episodes.map((e) => (
                <tr key={String(e.episode_id)}>
                  <td className="mono">{String(e.code)}</td>
                  <td>{String(e.scope)}</td>
                  <td className="num">{String(e.count)}</td>
                  <td>{String(e.severity)}</td>
                  <td>{e.recovered ? 'yes' : 'no'}</td>
                </tr>
              ))}
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
              <th>Age, s</th>
            </tr>
          </thead>
          <tbody>
            {events.map((e) => (
              <tr key={String(e.event_id)}>
                <td>{String(e.severity)}</td>
                <td className="mono">{String(e.code)}</td>
                <td>{String(e.title)}</td>
                <td className="num mono">
                  {typeof e.age_s === 'number' ? e.age_s.toFixed(1) : '—'}
                </td>
              </tr>
            ))}
            {events.length === 0 && (
              <tr>
                <td colSpan={4} className="muted">
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
