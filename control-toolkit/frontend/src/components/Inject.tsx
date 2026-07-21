/**
 * CAN Injector — debug-tool style field editors (no JSON typing).
 * Loads YAML dictionary fields; boolean / enum / number controls + live hex preview.
 */
import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { api } from '../api'
import { hexId } from '../lib/format'
import { useAppStore } from '../store'
import type { DictField, DictMessage } from './CanDictionary'
import { NumericDraft } from './NumericDraft'
import { WorkspaceShell } from './WorkspaceShell'

type FieldValue = number | boolean

type InjectTemplate = {
  id: string
  label: string
  description: string
  bus: 'high' | 'low'
  key: string
  values: Record<string, FieldValue>
  period_ms?: number
}

const TEMPLATES: InjectTemplate[] = [
  {
    id: 'host-drive-fwd',
    label: 'Host drive forward',
    description: 'HOST_DRIVE_CMD · 2 m/s · gear D',
    bus: 'high',
    key: 'host:host_drive_cmd',
    values: { speed_mmps: 2000, yaw_rate_mrad_s: 0, gear: 1 },
    period_ms: 10,
  },
  {
    id: 'host-drive-zero',
    label: 'Host drive zero',
    description: 'HOST_DRIVE_CMD · stop · gear N',
    bus: 'high',
    key: 'host:host_drive_cmd',
    values: { speed_mmps: 0, yaw_rate_mrad_s: 0, gear: 0 },
    period_ms: 10,
  },
  {
    id: 'host-drive-left',
    label: 'Host yaw left',
    description: 'HOST_DRIVE_CMD · slow + left yaw',
    bus: 'high',
    key: 'host:host_drive_cmd',
    values: { speed_mmps: 800, yaw_rate_mrad_s: -1200, gear: 1 },
    period_ms: 10,
  },
  {
    id: 'safety-estop',
    label: 'Safety ESTOP (DLC 0)',
    description: 'SAFETY_ESTOP on selected bus · empty payload',
    bus: 'high',
    key: 'safety:safety_estop',
    values: {},
  },
  {
    id: 'hmi-auto',
    label: 'HMI mode AUTO',
    description: 'HMI_MODE_REQ · AUTO · 1 Hz',
    bus: 'high',
    key: 'hmi:hmi_mode_req',
    values: { req_mode: 1, rolling_counter: 0 },
    period_ms: 1000,
  },
  {
    id: 'hmi-manual',
    label: 'HMI mode MANUAL',
    description: 'HMI_MODE_REQ · MANUAL · 1 Hz',
    bus: 'high',
    key: 'hmi:hmi_mode_req',
    values: { req_mode: 0, rolling_counter: 0 },
    period_ms: 1000,
  },
]

function defaultsFor(msg: DictMessage | null | undefined): Record<string, FieldValue> {
  const next: Record<string, FieldValue> = {}
  if (!msg) return next
  for (const field of msg.fields || []) {
    if (field.kind === 'boolean') {
      next[field.key] = false
    } else if (field.kind === 'enum' && field.options?.length) {
      const v = field.options[0].value
      next[field.key] = typeof v === 'number' ? v : Number(v) || 0
    } else if (field.min != null && field.min > 0) {
      next[field.key] = Number(field.min)
    } else {
      next[field.key] = 0
    }
  }
  // Sensible drive defaults
  if (msg.canonicalKey === 'host:host_drive_cmd' || msg.name === 'HOST_DRIVE_CMD') {
    next.speed_mmps = 0
    next.yaw_rate_mrad_s = 0
    next.gear = 0
  }
  return next
}

function fieldLabel(field: DictField): string {
  const unit = field.unit ? ` (${field.unit})` : ''
  return `${field.label || field.key}${unit}`
}

export function Inject() {
  const status = useAppStore((s) => s.status)
  const setStatus = useAppStore((s) => s.setStatus)
  const [messages, setMessages] = useState<DictMessage[]>([])
  const [mode, setMode] = useState<'named' | 'raw'>('named')
  const [bus, setBus] = useState<'high' | 'low'>('high')
  const [filter, setFilter] = useState('')
  const [selectedKey, setSelectedKey] = useState('')
  const [values, setValues] = useState<Record<string, FieldValue>>({})
  const [periodMs, setPeriodMs] = useState(50)
  const [periodic, setPeriodic] = useState(false)
  const [preview, setPreview] = useState<{
    data_hex?: string
    dlc?: number
    can_id?: number
    name?: string
    warnings?: string[]
    ok?: boolean
  } | null>(null)
  const [jobId, setJobId] = useState<string | null>(null)
  const [rawId, setRawId] = useState('0x300')
  const [rawHex, setRawHex] = useState('')
  const [rawExtended, setRawExtended] = useState(false)
  const [confirmRaw, setConfirmRaw] = useState(false)
  const [confirmEstop, setConfirmEstop] = useState(false)
  const [busy, setBusy] = useState(false)
  const [log, setLog] = useState('')
  /** Applied after selection settles so templates are not wiped by defaults. */
  const pendingValuesRef = useRef<Record<string, FieldValue> | null>(null)

  const benchOn = String(status?.session?.bench_tx ?? '').toLowerCase() === 'enabled'
  const sessionId = status?.session?.session_id

  const loadCatalog = useCallback(async () => {
    const d = await api.protocolDictionary()
    const msgs = (d.messages || []) as DictMessage[]
    setMessages(msgs)
  }, [])

  useEffect(() => {
    void loadCatalog().catch((e) => setLog(String(e)))
  }, [loadCatalog])

  const busMessages = useMemo(() => {
    const q = filter.trim().toLowerCase()
    return messages
      .filter((m) => m.bus === bus)
      .filter((m) => {
        // Prefer injectible messages; still list empty-field DLC0 (ESTOP)
        const caps = m.capabilities || {}
        const decoded = caps.decodedInjection !== false
        if (!decoded && (m.fields || []).length > 0) return false
        return true
      })
      .filter((m) => {
        if (!q) return true
        return (
          m.name.toLowerCase().includes(q) ||
          m.id.toLowerCase().includes(q) ||
          m.canonicalKey.toLowerCase().includes(q) ||
          (m.sender || '').toLowerCase().includes(q)
        )
      })
      .sort((a, b) => a.can_id - b.can_id || a.name.localeCompare(b.name))
  }, [messages, bus, filter])

  const selected = useMemo(() => {
    return (
      busMessages.find((m) => m.canonicalKey === selectedKey) ||
      busMessages.find((m) => `${m.bus}:${m.id}` === selectedKey) ||
      busMessages[0] ||
      null
    )
  }, [busMessages, selectedKey])

  // Reset values when selection changes (preserve pending template values)
  useEffect(() => {
    if (!selected) return
    setSelectedKey(selected.canonicalKey)
    if (pendingValuesRef.current) {
      setValues({ ...defaultsFor(selected), ...pendingValuesRef.current })
      pendingValuesRef.current = null
    } else {
      setValues(defaultsFor(selected))
    }
    setConfirmEstop(false)
    setPreview(null)
  }, [selected?.canonicalKey, selected?.bus])

  // Live preview when values change (debounced lightly via effect)
  useEffect(() => {
    if (mode !== 'named' || !selected) return
    let cancel = false
    const t = window.setTimeout(() => {
      void api
        .injectPreview({
          bus: selected.bus,
          key: selected.canonicalKey,
          values: values as Record<string, unknown>,
        })
        .then((r) => {
          if (!cancel) setPreview(r)
        })
        .catch(() => {
          if (!cancel) setPreview(null)
        })
    }, 120)
    return () => {
      cancel = true
      window.clearTimeout(t)
    }
  }, [mode, selected, values])

  function setField(key: string, value: FieldValue) {
    setValues((prev) => ({ ...prev, [key]: value }))
  }

  function applyTemplate(t: InjectTemplate) {
    setMode('named')
    pendingValuesRef.current = { ...t.values }
    setBus(t.bus)
    setSelectedKey(t.key)
    if (t.period_ms != null && t.period_ms > 0) {
      setPeriodic(true)
      setPeriodMs(t.period_ms)
    } else {
      setPeriodic(false)
    }
    setLog(`Template: ${t.label}`)
  }

  async function ensureSessionAndTx() {
    const st = await api.status()
    if (!st.session?.session_id) {
      throw new Error('No active session. Start Computer or Real in Settings first.')
    }
    if (st.session.bench_tx !== 'enabled') {
      throw new Error('Bench TX is off. Enable TX explicitly before inject.')
    }
    setStatus(st)
    return st
  }

  const isEstop =
    selected?.name === 'SAFETY_ESTOP' ||
    selected?.canonicalKey?.includes('safety_estop') ||
    selected?.can_id === 0x001

  async function doInject() {
    if (!selected) return
    setBusy(true)
    try {
      await ensureSessionAndTx()
      if (isEstop && !confirmEstop) {
        throw new Error('Confirm ESTOP injection before sending')
      }
      const r = await api.inject({
        bus: selected.bus,
        key: selected.canonicalKey,
        values: values as Record<string, unknown>,
        period_ms: periodic && periodMs > 0 ? periodMs : null,
        counter_field: selected.fields.some((f) => f.key === 'rolling_counter')
          ? 'rolling_counter'
          : null,
        owner: 'ui:inject',
      })
      if (r.job_id) {
        setJobId(r.job_id)
        setLog(`Periodic ${selected.name} job ${r.job_id} @ ${r.period_ms} ms`)
      } else {
        setLog(
          `Injected ${selected.name} · ${hexId(Number(r.can_id ?? selected.can_id))} · ${r.data_hex || '(empty)'}`,
        )
      }
      setStatus(await api.status())
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function doStop() {
    if (!jobId) return
    setBusy(true)
    try {
      await api.cancelInjection(jobId)
      setLog(`Stopped job ${jobId}`)
      setJobId(null)
      setStatus(await api.status())
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function doRaw() {
    setBusy(true)
    try {
      await ensureSessionAndTx()
      if (!confirmRaw) {
        throw new Error('Confirm raw/fault inject before sending')
      }
      const idStr = rawId.trim().toLowerCase().replace(/^0x/, '')
      const can_id = Number.parseInt(idStr, 16)
      if (!Number.isFinite(can_id)) throw new Error('Invalid CAN ID')
      const r = await api.injectRaw({
        bus,
        can_id,
        data_hex: rawHex,
        is_extended: rawExtended,
        confirm_raw: true,
      })
      setLog(`Raw inject ${hexId(can_id)} dlc=${r.dlc} · ${r.data_hex || '(empty)'}`)
      setStatus(await api.status())
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  const previewHex = preview?.data_hex ?? '—'
  const previewMeta = selected
    ? `${selected.id} · DLC ${preview?.dlc ?? selected.dlc} · ${selected.name}`
    : '—'

  return (
    <WorkspaceShell
      testId="workspace-inject"
      title="Inject"
      description={
        <>
          Named inject with dictionary field editors (like debug-tool). No JSON. Requires session +{' '}
          <strong>Bench TX</strong>.
        </>
      }
    >
      <section className="panel" data-testid="inject-gate">
        <div className="control-status-row">
          <div className="control-status-item">
            <span className="muted small">Session</span>
            <strong className="mono">{sessionId ?? 'none'}</strong>
          </div>
          <div className="control-status-item">
            <span className="muted small">Bench TX</span>
            <strong className={benchOn ? 'ok-text' : 'danger-text'} data-testid="inject-bench-tx">
              {benchOn ? 'Armed' : 'Off — enable before inject'}
            </strong>
          </div>
          <div className="control-status-item">
            <span className="muted small">Wire preview</span>
            <strong className="mono" data-testid="inject-preview-hex">
              {mode === 'named' ? previewHex || '(empty)' : rawHex || '(empty)'}
            </strong>
          </div>
        </div>
        <div className="seg" style={{ marginTop: 10 }}>
          <button
            type="button"
            className={mode === 'named' ? 'seg-btn active' : 'seg-btn'}
            data-testid="inject-mode-named"
            onClick={() => setMode('named')}
          >
            Named message
          </button>
          <button
            type="button"
            className={mode === 'raw' ? 'seg-btn active' : 'seg-btn'}
            data-testid="inject-mode-raw"
            onClick={() => setMode('raw')}
          >
            Raw / fault
          </button>
        </div>
      </section>

      {mode === 'named' ? (
        <div className="inject-layout" data-testid="inject-named-panel">
          <section className="panel inject-main">
            <div className="panel-title-row">
              <h2>Message</h2>
              <span className="mono muted small">{previewMeta}</span>
            </div>

            <div className="seg" data-testid="inject-bus-tabs">
              {(['high', 'low'] as const).map((b) => (
                <button
                  key={b}
                  type="button"
                  className={bus === b ? 'seg-btn active' : 'seg-btn'}
                  data-testid={`inject-bus-${b}`}
                  onClick={() => {
                    setBus(b)
                    setSelectedKey('')
                  }}
                >
                  {b.toUpperCase()} bus
                </button>
              ))}
            </div>

            <label className="field" style={{ marginTop: 10 }}>
              <span className="field-label">Filter</span>
              <input
                data-testid="inject-filter"
                placeholder="Name, ID, key, sender…"
                value={filter}
                onChange={(e) => setFilter(e.target.value)}
              />
            </label>

            <label className="field">
              <span className="field-label">CAN message</span>
              <select
                data-testid="inject-message"
                value={selected?.canonicalKey ?? ''}
                onChange={(e) => setSelectedKey(e.target.value)}
              >
                {busMessages.length === 0 && <option value="">No messages on {bus}</option>}
                {busMessages.map((m) => (
                  <option key={`${m.bus}-${m.canonicalKey}-${m.can_id}`} value={m.canonicalKey}>
                    {m.id} {m.name}
                    {m.sender && m.sender !== '—' ? ` · ${m.sender}` : ''}
                    {(m.fields || []).length === 0 ? ' · (no fields / event)' : ''}
                  </option>
                ))}
              </select>
            </label>

            {selected && (selected.fields || []).length > 0 ? (
              <div className="form-grid inject-fields" data-testid="inject-fields">
                {selected.fields.map((field) => (
                  <label key={field.key} className="field" data-testid={`inject-field-${field.key}`}>
                    <span className="field-label" title={field.comment || field.key}>
                      {fieldLabel(field)}
                      {field.min != null || field.max != null ? (
                        <span className="muted small">
                          {' '}
                          [{field.min ?? '—'}…{field.max ?? '—'}]
                        </span>
                      ) : null}
                    </span>
                    {field.kind === 'boolean' ? (
                      <input
                        type="checkbox"
                        data-testid={`inject-val-${field.key}`}
                        checked={Boolean(values[field.key])}
                        onChange={(e) => setField(field.key, e.target.checked)}
                      />
                    ) : field.kind === 'enum' && field.options?.length ? (
                      <select
                        data-testid={`inject-val-${field.key}`}
                        value={String(values[field.key] ?? field.options[0]?.value ?? 0)}
                        onChange={(e) => {
                          const raw = e.target.value
                          const asNum = Number(raw)
                          setField(field.key, Number.isFinite(asNum) ? asNum : (raw as unknown as number))
                        }}
                      >
                        {field.options.map((opt) => (
                          <option key={String(opt.value)} value={String(opt.value)}>
                            {opt.label} ({String(opt.value)})
                          </option>
                        ))}
                      </select>
                    ) : (
                      <NumericDraft
                        testId={`inject-val-${field.key}`}
                        value={Number(values[field.key] ?? 0)}
                        min={field.min ?? undefined}
                        max={field.max ?? undefined}
                        onValue={(n) => setField(field.key, n)}
                      />
                    )}
                  </label>
                ))}
              </div>
            ) : selected ? (
              <p className="muted small" data-testid="inject-no-fields">
                No editable signals (event / empty payload). Send uses empty or codec defaults.
              </p>
            ) : null}

            {isEstop && (
              <label className="check-row" data-testid="inject-confirm-estop">
                <input
                  type="checkbox"
                  checked={confirmEstop}
                  onChange={(e) => setConfirmEstop(e.target.checked)}
                />
                Confirm ESTOP injection (DLC=0 safety frame)
              </label>
            )}

            <div className="field-row inject-period-row" style={{ marginTop: 12, alignItems: 'end' }}>
              <label className="check-row">
                <input
                  type="checkbox"
                  data-testid="inject-periodic"
                  checked={periodic}
                  onChange={(e) => setPeriodic(e.target.checked)}
                />
                Periodic
              </label>
              <label className="field">
                <span className="field-label">Period ms</span>
                <NumericDraft
                  testId="inject-period"
                  value={periodMs}
                  min={1}
                  max={60000}
                  disabled={!periodic}
                  onValue={setPeriodMs}
                />
              </label>
              {selected?.fields.some((f) => f.key === 'rolling_counter') ? (
                <span className="muted small">rolling_counter auto-increments when periodic</span>
              ) : null}
            </div>

            {preview?.warnings?.length ? (
              <p className="danger-text small" data-testid="inject-warnings">
                Preview warnings: {preview.warnings.join(', ')}
              </p>
            ) : null}

            <div className="actions tight" style={{ marginTop: 12 }}>
              <button
                type="button"
                disabled={busy || !selected}
                data-testid="inject-submit"
                onClick={() => void doInject()}
              >
                {periodic ? 'Start periodic' : 'Send once'}
              </button>
              <button
                type="button"
                className="secondary"
                disabled={busy || !jobId}
                data-testid="inject-stop"
                onClick={() => void doStop()}
              >
                Stop job
              </button>
            </div>
          </section>

          <section className="panel inject-side" data-testid="inject-templates">
            <div className="panel-title-row">
              <h2>Templates</h2>
              <span className="muted small">{TEMPLATES.length}</span>
            </div>
            <p className="muted small" style={{ marginTop: 0 }}>
              One click fills bus, message, and signal fields.
            </p>
            <div className="inject-template-list">
              {TEMPLATES.map((t) => (
                <button
                  key={t.id}
                  type="button"
                  className="inject-template-btn"
                  data-testid={`inject-template-${t.id}`}
                  onClick={() => applyTemplate(t)}
                >
                  <strong>{t.label}</strong>
                  <span className="muted small">{t.description}</span>
                </button>
              ))}
            </div>

            <h3 style={{ marginTop: 16 }}>Live values</h3>
            <dl className="kv compact" data-testid="inject-value-summary">
              {Object.entries(values).map(([k, v]) => (
                <FragmentPair key={k} k={k} v={v} />
              ))}
              {Object.keys(values).length === 0 && (
                <>
                  <dt>—</dt>
                  <dd className="muted">No signals</dd>
                </>
              )}
            </dl>
          </section>
        </div>
      ) : (
        <section className="panel" data-testid="inject-raw-panel">
          <h2>Raw / fault inject</h2>
          <p className="control-callout">
            Expert path — <strong>no codec validation</strong>. Prefer named inject for normal tests.
          </p>
          <div className="field-row">
            <label className="field">
              <span className="field-label">Bus</span>
              <select
                data-testid="raw-bus"
                value={bus}
                onChange={(e) => setBus(e.target.value as 'high' | 'low')}
              >
                <option value="high">high</option>
                <option value="low">low</option>
              </select>
            </label>
            <label className="field">
              <span className="field-label">CAN ID (hex)</span>
              <input
                data-testid="raw-can-id"
                className="mono"
                value={rawId}
                onChange={(e) => setRawId(e.target.value)}
              />
            </label>
          </div>
          <label className="field">
            <span className="field-label">Data hex (optional, even length)</span>
            <input
              data-testid="raw-data-hex"
              className="mono"
              placeholder="e.g. 00ffaabb or empty DLC=0"
              value={rawHex}
              onChange={(e) => setRawHex(e.target.value)}
            />
          </label>
          <label className="check-row">
            <input
              type="checkbox"
              data-testid="raw-extended"
              checked={rawExtended}
              onChange={(e) => setRawExtended(e.target.checked)}
            />
            Extended ID
          </label>
          <label className="check-row">
            <input
              type="checkbox"
              data-testid="raw-confirm"
              checked={confirmRaw}
              onChange={(e) => setConfirmRaw(e.target.checked)}
            />
            I confirm this is intentional fault injection
          </label>
          <div className="actions tight">
            <button type="button" disabled={busy} data-testid="raw-submit" onClick={() => void doRaw()}>
              Send raw frame
            </button>
          </div>
        </section>
      )}

      {log ? (
        <p className="mono small" data-testid="inject-log">
          {log}
        </p>
      ) : null}
    </WorkspaceShell>
  )
}

function FragmentPair({ k, v }: { k: string; v: FieldValue }) {
  return (
    <>
      <dt className="mono">{k}</dt>
      <dd className="mono">{String(v)}</dd>
    </>
  )
}
