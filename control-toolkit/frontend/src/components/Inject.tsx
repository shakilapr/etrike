/**
 * CAN Injector — signal-tailored CAN generator & interactive controller manager.
 * Loads YAML protocol contract definitions; boolean / enum / number range controls + live wire preview.
 * Includes interactive Started Controllers sidebar (Start/Stop/Load), templates, and command logs.
 */
import { useCallback, useEffect, useMemo, useState } from 'react'
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

type RawPreset = {
  id: string
  label: string
  description: string
  bus: 'high' | 'low'
  can_id: string
  data_hex: string
  is_extended?: boolean
}

type ActiveJob = {
  job_id: string
  bus: string
  key?: string | null
  can_id?: number | null
  values: Record<string, unknown>
  period_ms: number
  owner: string
  counter_field?: string | null
  missed: number
  last_result?: string | null
}

type StartedController = {
  id: string
  bus: 'high' | 'low'
  key: string
  can_id: string
  name: string
  period_ms: number
  values: Record<string, FieldValue>
  status: 'RUNNING' | 'STOPPED'
  job_id?: string | null
  last_started_at: string
}

type AckLog = {
  id: string
  timestamp: string
  type: 'ONESHOT' | 'PERIODIC' | 'RAW' | 'STOP' | 'ARM'
  bus: string
  can_id: string
  name: string
  data_hex: string
  ok: boolean
  detail: string
}

const SIGNAL_PRESETS: Record<string, Array<{ label: string; value: number }>> = {
  speed_mmps: [
    { label: 'Rev (-0.5m/s)', value: -500 },
    { label: 'Stop (0m/s)', value: 0 },
    { label: 'Slow (0.8m/s)', value: 800 },
    { label: 'Drive (2m/s)', value: 2000 },
    { label: 'Max (3m/s)', value: 3000 },
  ],
  motor_speed_mmps: [
    { label: 'Rev (-0.5m/s)', value: -500 },
    { label: 'Stop (0m/s)', value: 0 },
    { label: 'Drive (2m/s)', value: 2000 },
    { label: 'Max (3m/s)', value: 3000 },
  ],
  actual_speed_mmps: [
    { label: 'Stop (0m/s)', value: 0 },
    { label: 'Drive (2m/s)', value: 2000 },
    { label: 'Max (3m/s)', value: 3000 },
  ],
  yaw_rate_mrad_s: [
    { label: 'Left (-1.5rad/s)', value: -1500 },
    { label: 'Straight (0)', value: 0 },
    { label: 'Right (+1.5rad/s)', value: 1500 },
  ],
  brake_pressure_kpa: [
    { label: 'Off (0 kPa)', value: 0 },
    { label: 'Light (5k)', value: 5000 },
    { label: 'Med (10k)', value: 10000 },
    { label: 'Full (20k)', value: 20000 },
  ],
  gear: [
    { label: 'N (0)', value: 0 },
    { label: 'D (1)', value: 1 },
    { label: 'S (2)', value: 2 },
    { label: 'R (3)', value: 3 },
  ],
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
    description: 'SAFETY_ESTOP on HIGH bus · empty payload',
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
  {
    id: 'sys-heartbeat',
    label: 'Sys Heartbeat OK',
    description: 'SYS_HEARTBEAT on LOW bus · 100 ms',
    bus: 'low',
    key: 'sys:sys_heartbeat',
    values: {
      alive_ctr: 0,
      heartbeat_ok: 1,
      estop_active: 0,
      mode_auto: 1,
      can_ok: 1,
      task_safety_ok: 1,
      task_brake_ok: 1,
      task_dispatch_ok: 1,
      task_can_tx_ok: 1,
    },
    period_ms: 100,
  },
]

const RAW_PRESETS: RawPreset[] = [
  {
    id: 'corrupt-payload',
    label: 'Corrupt Payload (0xFF)',
    description: '0x300 HOST_DRIVE_CMD with invalid 0xFF payload',
    bus: 'high',
    can_id: '0x300',
    data_hex: 'ffffffffffffffff',
  },
  {
    id: 'dlc-mismatch',
    label: 'DLC Mismatch (Short)',
    description: '0x300 with incomplete 4-byte payload',
    bus: 'high',
    can_id: '0x300',
    data_hex: '00000000',
  },
  {
    id: 'unmapped-id',
    label: 'Unmapped CAN ID (0x7FF)',
    description: 'Transmit unmapped diagnostic ID frame',
    bus: 'high',
    can_id: '0x7FF',
    data_hex: '11223344',
  },
  {
    id: 'estop-raw',
    label: 'Raw ESTOP (0x001)',
    description: 'Empty DLC 0 frame to trigger safety stop',
    bus: 'high',
    can_id: '0x001',
    data_hex: '',
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
  if (msg.canonicalKey === 'host:host_drive_cmd' || msg.name === 'HOST_DRIVE_CMD') {
    next.speed_mmps = 2000
    next.yaw_rate_mrad_s = 0
    next.gear = 1
  }
  return next
}

function fieldLabel(field: DictField): string {
  const unit = field.unit ? ` (${field.unit})` : ''
  return `${field.label || field.key}${unit}`
}

function formatBytes(hex?: string): string {
  if (!hex) return '(empty)'
  const clean = hex.replace(/\s+/g, '')
  const pairs: string[] = []
  for (let i = 0; i < clean.length; i += 2) {
    pairs.push(`0x${clean.slice(i, i + 2).toUpperCase()}`)
  }
  return `[${pairs.join(', ')}]`
}

function formatSignalSummary(msg: DictMessage | null, values: Record<string, FieldValue>): string {
  if (!msg || !msg.fields || msg.fields.length === 0) return 'No signals (DLC 0)'
  return msg.fields
    .map((f) => {
      const val = values[f.key] ?? 0
      if (f.kind === 'enum' && f.options) {
        const opt = f.options.find((o) => String(o.value) === String(val))
        return `${f.key}: ${opt ? opt.label : val}`
      }
      if (f.kind === 'boolean') {
        return `${f.key}: ${val ? 'ON' : 'OFF'}`
      }
      const unitStr = f.unit ? ` ${f.unit}` : ''
      return `${f.key}: ${val}${unitStr}`
    })
    .join(' · ')
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
  
  // Active Periodic Jobs & Started Controllers
  const [activeJobs, setActiveJobs] = useState<ActiveJob[]>([])
  const [startedControllers, setStartedControllers] = useState<Record<string, StartedController>>({})
  const [ackLogs, setAckLogs] = useState<AckLog[]>([])
  
  // Raw mode state
  const [rawId, setRawId] = useState('0x300')
  const [rawHex, setRawHex] = useState('')
  const [rawExtended, setRawExtended] = useState(false)
  const [confirmRaw, setConfirmRaw] = useState(false)
  const [confirmEstop, setConfirmEstop] = useState(false)
  
  const [busy, setBusy] = useState(false)
  const [log, setLog] = useState('')

  const benchOn = String(status?.session?.bench_tx ?? '').toLowerCase() === 'enabled'
  const sessionId = status?.session?.session_id

  const loadCatalog = useCallback(async () => {
    const d = await api.protocolDictionary()
    const msgs = (d.messages || []) as DictMessage[]
    setMessages(msgs)
  }, [])

  const fetchJobs = useCallback(async () => {
    try {
      const res = await api.injectionJobs()
      const jobs = res.jobs || []
      setActiveJobs(jobs)

      // Sync startedControllers state with backend running jobs
      setStartedControllers((prev) => {
        const next = { ...prev }
        const jobMap = new Map(jobs.map((j) => [`${j.bus}:${j.key || j.can_id}`, j]))
        
        // Update existing controllers
        for (const ctrlKey of Object.keys(next)) {
          const ctrl = next[ctrlKey]
          const backendJob = jobMap.get(`${ctrl.bus}:${ctrl.key}`) || jobMap.get(`${ctrl.bus}:${ctrl.can_id}`)
          if (backendJob) {
            next[ctrlKey] = {
              ...ctrl,
              status: 'RUNNING',
              job_id: backendJob.job_id,
            }
          } else if (ctrl.status === 'RUNNING') {
            next[ctrlKey] = {
              ...ctrl,
              status: 'STOPPED',
              job_id: null,
            }
          }
        }
        return next
      })
    } catch {
      // Ignore poll errors if backend offline
    }
  }, [])

  useEffect(() => {
    void loadCatalog().catch((e) => setLog(String(e)))
    void fetchJobs()
    const timer = setInterval(() => {
      void fetchJobs()
    }, 2000)
    return () => clearInterval(timer)
  }, [loadCatalog, fetchJobs])

  const busMessages = useMemo(() => {
    const q = filter.trim().toLowerCase()
    return messages
      .filter((m) => m.bus === bus)
      .filter((m) => {
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

  // Select first message when switching bus if selectedKey isn't on new bus
  const handleBusChange = (newBus: 'high' | 'low') => {
    setBus(newBus)
    const firstOnBus = messages.find((m) => m.bus === newBus)
    if (firstOnBus) {
      setSelectedKey(firstOnBus.canonicalKey)
      setValues(defaultsFor(firstOnBus))
    } else {
      setSelectedKey('')
      setValues({})
    }
    setConfirmEstop(false)
  }

  // Handle explicit message selection change
  const handleMessageChange = (key: string) => {
    setSelectedKey(key)
    const msg = busMessages.find((m) => m.canonicalKey === key)
    if (msg) {
      setValues(defaultsFor(msg))
    }
    setConfirmEstop(false)
  }

  // Live preview when values change
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
    }, 100)
    return () => {
      cancel = true
      window.clearTimeout(t)
    }
  }, [mode, selected, values])

  function setField(key: string, value: FieldValue) {
    setValues((prev) => ({ ...prev, [key]: value }))
  }

  function addAckLog(item: Omit<AckLog, 'id'>) {
    const id = `ack_${Date.now()}_${Math.random().toString(36).substring(2, 7)}`
    setAckLogs((prev) => [{ id, ...item }, ...prev.slice(0, 49)])
  }

  function applyTemplate(t: InjectTemplate) {
    setMode('named')
    setBus(t.bus)
    const msg = messages.find((m) => m.canonicalKey === t.key || `${m.bus}:${m.id}` === t.key)
    setSelectedKey(t.key)
    setValues({ ...(msg ? defaultsFor(msg) : {}), ...t.values })
    if (t.period_ms != null && t.period_ms > 0) {
      setPeriodic(true)
      setPeriodMs(t.period_ms)
    } else {
      setPeriodic(false)
    }
    setConfirmEstop(false)
    setLog(`Template loaded: ${t.label}`)
  }

  function applyRawPreset(p: RawPreset) {
    setMode('raw')
    setBus(p.bus)
    setRawId(p.can_id)
    setRawHex(p.data_hex)
    setRawExtended(Boolean(p.is_extended))
    setConfirmRaw(true)
    setLog(`Raw preset loaded: ${p.label}`)
  }

  async function ensureSessionAndTx() {
    const st = await api.status()
    if (!st.session?.session_id) {
      throw new Error('No active session. Create or start a session in Settings first.')
    }
    if (st.session.bench_tx !== 'enabled') {
      throw new Error('Bench TX is disabled. Arm Bench TX before injecting CAN frames.')
    }
    setStatus(st)
    return st
  }

  async function enableBenchTx() {
    if (!sessionId) {
      setLog('No active session to arm.')
      return
    }
    setBusy(true)
    try {
      const rev = Number(status?.session?.revision ?? 0)
      await api.setBenchTx(String(sessionId), true, rev)
      setStatus(await api.status())
      addAckLog({
        timestamp: new Date().toLocaleTimeString(),
        type: 'ARM',
        bus: 'both',
        can_id: '—',
        name: 'BENCH_TX_ARM',
        data_hex: '',
        ok: true,
        detail: 'Bench TX armed successfully',
      })
      setLog('Bench TX armed successfully.')
    } catch (e) {
      setLog(`Failed to arm Bench TX: ${String(e)}`)
    } finally {
      setBusy(false)
    }
  }

  const isEstop =
    selected?.name === 'SAFETY_ESTOP' ||
    selected?.canonicalKey?.includes('safety_estop') ||
    selected?.can_id === 0x001

  async function doInject() {
    if (!selected) return
    setBusy(true)
    const timestamp = new Date().toLocaleTimeString()
    try {
      await ensureSessionAndTx()
      if (isEstop && !confirmEstop) {
        throw new Error('Confirm ESTOP injection checkbox before sending.')
      }

      const isPeriodic = periodic && periodMs > 0
      const r = await api.inject({
        bus: selected.bus,
        key: selected.canonicalKey,
        values: values as Record<string, unknown>,
        period_ms: isPeriodic ? periodMs : null,
        counter_field: selected.fields.some((f) => f.key === 'rolling_counter')
          ? 'rolling_counter'
          : null,
        owner: 'ui:inject',
      })

      const dataHex = r.data_hex || preview?.data_hex || ''
      const hexText = hexId(Number(r.can_id ?? selected.can_id))

      if (r.job_id) {
        setLog(`Scheduled periodic ${selected.name} (${hexText}) @ ${r.period_ms} ms [job ${r.job_id}]`)
        addAckLog({
          timestamp,
          type: 'PERIODIC',
          bus: selected.bus,
          can_id: hexText,
          name: selected.name,
          data_hex: dataHex,
          ok: true,
          detail: `Periodic scheduled @ ${r.period_ms} ms (job ${r.job_id})`,
        })

        // Track in Started Controllers sidebar
        const ctrlId = `${selected.bus}:${selected.canonicalKey}`
        setStartedControllers((prev) => ({
          ...prev,
          [ctrlId]: {
            id: ctrlId,
            bus: selected.bus as 'high' | 'low',
            key: selected.canonicalKey,
            can_id: hexText,
            name: selected.name,
            period_ms: r.period_ms || periodMs,
            values: { ...values },
            status: 'RUNNING',
            job_id: r.job_id,
            last_started_at: timestamp,
          },
        }))

        await fetchJobs()
      } else {
        setLog(`Injected ${selected.name} (${hexText}) · ${formatBytes(dataHex)}`)
        addAckLog({
          timestamp,
          type: 'ONESHOT',
          bus: selected.bus,
          can_id: hexText,
          name: selected.name,
          data_hex: dataHex,
          ok: true,
          detail: `One-shot injected on ${selected.bus} bus (req ${r.request_id || 'ok'})`,
        })
      }
      setStatus(await api.status())
    } catch (e) {
      const msg = String(e)
      setLog(msg)
      addAckLog({
        timestamp,
        type: periodic ? 'PERIODIC' : 'ONESHOT',
        bus: selected?.bus ?? bus,
        can_id: hexId(selected?.can_id ?? 0),
        name: selected?.name ?? 'UNKNOWN',
        data_hex: preview?.data_hex || '',
        ok: false,
        detail: msg,
      })
    } finally {
      setBusy(false)
    }
  }

  async function startController(ctrl: StartedController) {
    setBusy(true)
    const timestamp = new Date().toLocaleTimeString()
    try {
      await ensureSessionAndTx()
      const msg = messages.find((m) => m.canonicalKey === ctrl.key)
      const r = await api.inject({
        bus: ctrl.bus,
        key: ctrl.key,
        values: ctrl.values as Record<string, unknown>,
        period_ms: ctrl.period_ms,
        counter_field: msg?.fields.some((f) => f.key === 'rolling_counter') ? 'rolling_counter' : null,
        owner: 'ui:inject',
      })
      if (r.job_id) {
        setLog(`Resumed periodic ${ctrl.name} @ ${ctrl.period_ms} ms [job ${r.job_id}]`)
        addAckLog({
          timestamp,
          type: 'PERIODIC',
          bus: ctrl.bus,
          can_id: ctrl.can_id,
          name: ctrl.name,
          data_hex: r.data_hex || '',
          ok: true,
          detail: `Resumed periodic transmission @ ${ctrl.period_ms} ms`,
        })
        setStartedControllers((prev) => ({
          ...prev,
          [ctrl.id]: {
            ...ctrl,
            status: 'RUNNING',
            job_id: r.job_id,
            last_started_at: timestamp,
          },
        }))
        await fetchJobs()
      }
      setStatus(await api.status())
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function stopController(ctrl: StartedController) {
    if (!ctrl.job_id) return
    setBusy(true)
    const timestamp = new Date().toLocaleTimeString()
    try {
      await api.cancelInjection(ctrl.job_id)
      setLog(`Stopped controller ${ctrl.name} (${ctrl.job_id})`)
      addAckLog({
        timestamp,
        type: 'STOP',
        bus: ctrl.bus,
        can_id: ctrl.can_id,
        name: ctrl.name,
        data_hex: '',
        ok: true,
        detail: `Stopped controller loop ${ctrl.name}`,
      })
      setStartedControllers((prev) => ({
        ...prev,
        [ctrl.id]: {
          ...ctrl,
          status: 'STOPPED',
          job_id: null,
        },
      }))
      await fetchJobs()
      setStatus(await api.status())
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  function loadControllerSettings(ctrl: StartedController) {
    setMode('named')
    setBus(ctrl.bus)
    setSelectedKey(ctrl.key)
    setValues({ ...ctrl.values })
    setPeriodic(true)
    setPeriodMs(ctrl.period_ms)
    setLog(`Loaded settings for ${ctrl.name}`)
  }

  async function doStopJob(jobId: string) {
    setBusy(true)
    const timestamp = new Date().toLocaleTimeString()
    try {
      await api.cancelInjection(jobId)
      setLog(`Stopped job ${jobId}`)
      addAckLog({
        timestamp,
        type: 'STOP',
        bus: 'system',
        can_id: '—',
        name: 'CANCEL_JOB',
        data_hex: '',
        ok: true,
        detail: `Canceled periodic job ${jobId}`,
      })
      await fetchJobs()
      setStatus(await api.status())
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function doStopAllJobs() {
    setBusy(true)
    const timestamp = new Date().toLocaleTimeString()
    try {
      const res = await api.cancelAllInjections()
      setLog(`Canceled ${res.canceled_count} active periodic job(s)`)
      addAckLog({
        timestamp,
        type: 'STOP',
        bus: 'system',
        can_id: '—',
        name: 'CANCEL_ALL',
        data_hex: '',
        ok: true,
        detail: `Canceled all ${res.canceled_count} periodic job(s)`,
      })
      setStartedControllers((prev) => {
        const next = { ...prev }
        for (const k of Object.keys(next)) {
          next[k] = { ...next[k], status: 'STOPPED', job_id: null }
        }
        return next
      })
      await fetchJobs()
      setStatus(await api.status())
    } catch (e) {
      setLog(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function doRaw() {
    setBusy(true)
    const timestamp = new Date().toLocaleTimeString()
    try {
      await ensureSessionAndTx()
      if (!confirmRaw) {
        throw new Error('Check the raw injection confirmation checkbox before sending.')
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
      const formattedId = hexId(can_id)
      setLog(`Raw inject ${formattedId} dlc=${r.dlc} · ${formatBytes(r.data_hex)}`)
      addAckLog({
        timestamp,
        type: 'RAW',
        bus,
        can_id: formattedId,
        name: 'RAW_FRAME',
        data_hex: r.data_hex,
        ok: true,
        detail: `Raw frame submitted (dlc=${r.dlc})`,
      })
      setStatus(await api.status())
    } catch (e) {
      const msg = String(e)
      setLog(msg)
      addAckLog({
        timestamp,
        type: 'RAW',
        bus,
        can_id: rawId,
        name: 'RAW_FRAME',
        data_hex: rawHex,
        ok: false,
        detail: msg,
      })
    } finally {
      setBusy(false)
    }
  }

  const previewHex = preview?.data_hex ?? ''
  const previewMeta = selected
    ? `${selected.id} · DLC ${preview?.dlc ?? selected.dlc} · ${selected.name}`
    : '—'

  const startedList = useMemo(() => Object.values(startedControllers), [startedControllers])

  return (
    <WorkspaceShell
      testId="workspace-inject"
      title="CAN Generator & Injector"
      description={
        <>
          Interactive CAN frame generator & fault injection toolkit with signal-tailored controls.
        </>
      }
    >
      {/* Top Banner / Session & Bench TX Status */}
      <section className="panel" data-testid="inject-gate">
        <div className="control-status-row" style={{ alignItems: 'center', justifyContent: 'space-between' }}>
          <div style={{ display: 'flex', gap: '20px', alignItems: 'center' }}>
            <div className="control-status-item">
              <span className="muted small">Session</span>
              <strong className="mono">{sessionId ?? 'none'}</strong>
            </div>
            <div className="control-status-item">
              <span className="muted small">Bench TX</span>
              <strong className={benchOn ? 'ok-text' : 'danger-text'} data-testid="inject-bench-tx">
                {benchOn ? 'Armed' : 'Off'}
              </strong>
            </div>
            <div className="control-status-item">
              <span className="muted small">Wire Preview</span>
              <strong className="mono" data-testid="inject-preview-hex" style={{ color: 'var(--primary)' }}>
                {mode === 'named'
                  ? formatBytes(previewHex)
                  : formatBytes(rawHex)}
              </strong>
            </div>
          </div>

          {!benchOn && (
            <button
              type="button"
              className="primary"
              disabled={busy || !sessionId}
              style={{ backgroundColor: 'var(--warning)', borderColor: 'var(--warning)' }}
              onClick={() => void enableBenchTx()}
            >
              Arm Bench TX
            </button>
          )}
        </div>

        {/* Mode Switcher */}
        <div className="seg" style={{ marginTop: 14 }}>
          <button
            type="button"
            className={mode === 'named' ? 'seg-btn active' : 'seg-btn'}
            data-testid="inject-mode-named"
            onClick={() => setMode('named')}
          >
            Named Messages
          </button>
          <button
            type="button"
            className={mode === 'raw' ? 'seg-btn active' : 'seg-btn'}
            data-testid="inject-mode-raw"
            onClick={() => setMode('raw')}
          >
            Raw / Fault Inject
          </button>
        </div>
      </section>

      {/* Main Workspace Layout */}
      {mode === 'named' ? (
        <div className="inject-layout" data-testid="inject-named-panel">
          {/* Main Injector Panel */}
          <section className="panel inject-main">
            <div className="panel-title-row">
              <h2>Signal Generator</h2>
              <span className="mono muted small">{previewMeta}</span>
            </div>

            {/* Bus selector tabs */}
            <div className="seg" data-testid="inject-bus-tabs">
              {(['high', 'low'] as const).map((b) => (
                <button
                  key={b}
                  type="button"
                  className={bus === b ? 'seg-btn active' : 'seg-btn'}
                  data-testid={`inject-bus-${b}`}
                  onClick={() => handleBusChange(b)}
                >
                  {b.toUpperCase()} Bus
                </button>
              ))}
            </div>

            <div className="field-row" style={{ marginTop: 10 }}>
              <label className="field" style={{ flex: 1 }}>
                <span className="field-label">Filter Messages</span>
                <input
                  data-testid="inject-filter"
                  placeholder="Search name, ID (0x300), key, sender…"
                  value={filter}
                  onChange={(e) => setFilter(e.target.value)}
                />
              </label>

              <label className="field" style={{ flex: 2 }}>
                <span className="field-label">CAN Message</span>
                <select
                  data-testid="inject-message"
                  value={selected?.canonicalKey ?? ''}
                  onChange={(e) => handleMessageChange(e.target.value)}
                >
                  {busMessages.length === 0 && <option value="">No messages matching on {bus}</option>}
                  {busMessages.map((m) => (
                    <option key={`${m.bus}-${m.canonicalKey}-${m.can_id}`} value={m.canonicalKey}>
                      {m.id} {m.name}
                      {m.sender && m.sender !== '—' ? ` · ${m.sender}` : ''}
                      {(m.fields || []).length === 0 ? ' · (DLC 0 / event)' : ''}
                    </option>
                  ))}
                </select>
              </label>
            </div>

            {/* Signal-Tailored Field Editors */}
            {selected && (selected.fields || []).length > 0 ? (
              <div className="form-grid inject-fields" data-testid="inject-fields" style={{ marginTop: 12 }}>
                {selected.fields.map((field) => {
                  const presets = SIGNAL_PRESETS[field.key] || []
                  const curVal = Number(values[field.key] ?? 0)
                  const hasSlider = field.kind !== 'boolean' && field.kind !== 'enum' && field.min != null && field.max != null

                  return (
                    <div key={field.key} className="field" data-testid={`inject-field-${field.key}`}>
                      <div className="panel-title-row" style={{ marginBottom: 2 }}>
                        <span className="field-label" title={field.comment || field.key}>
                          {fieldLabel(field)}
                        </span>
                        {field.min != null || field.max != null ? (
                          <span className="muted small mono">
                            [{field.min ?? '—'}…{field.max ?? '—'}]
                          </span>
                        ) : null}
                      </div>

                      {field.kind === 'boolean' ? (
                        <div style={{ padding: '4px 0', display: 'flex', alignItems: 'center', gap: 8 }}>
                          <input
                            type="checkbox"
                            data-testid={`inject-val-${field.key}`}
                            checked={Boolean(values[field.key])}
                            onChange={(e) => setField(field.key, e.target.checked)}
                          />
                          <span className={`small ${values[field.key] ? 'ok-text' : 'muted'}`}>
                            {values[field.key] ? 'ACTIVE (1)' : 'INACTIVE (0)'}
                          </span>
                        </div>
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
                        <div>
                          <div className="signal-slider-row">
                            {hasSlider && (
                              <input
                                type="range"
                                className="signal-slider"
                                min={field.min ?? 0}
                                max={field.max ?? 100}
                                step={1}
                                value={curVal}
                                onChange={(e) => setField(field.key, Number(e.target.value))}
                              />
                            )}
                            <div style={{ width: hasSlider ? '90px' : '100%' }}>
                              <NumericDraft
                                testId={`inject-val-${field.key}`}
                                value={curVal}
                                min={field.min ?? undefined}
                                max={field.max ?? undefined}
                                onValue={(n) => setField(field.key, n)}
                              />
                            </div>
                          </div>

                          {presets.length > 0 && (
                            <div className="signal-presets">
                              {presets.map((p) => (
                                <button
                                  key={p.label}
                                  type="button"
                                  className="preset-chip"
                                  onClick={() => setField(field.key, p.value)}
                                >
                                  {p.label}
                                </button>
                              ))}
                            </div>
                          )}
                        </div>
                      )}
                    </div>
                  )
                })}
              </div>
            ) : selected ? (
              <p className="muted small" data-testid="inject-no-fields" style={{ marginTop: 10 }}>
                This message has no configurable payload fields (event trigger or DLC 0 frame).
              </p>
            ) : null}

            {/* ESTOP Safety Confirmation */}
            {isEstop && (
              <label className="check-row danger-text" data-testid="inject-confirm-estop" style={{ marginTop: 14 }}>
                <input
                  type="checkbox"
                  checked={confirmEstop}
                  onChange={(e) => setConfirmEstop(e.target.checked)}
                />
                Confirm ESTOP Injection (Safety frame trigger)
              </label>
            )}

            {/* Periodic Transmission Settings */}
            <div className="field-row inject-period-row" style={{ marginTop: 14, alignItems: 'end' }}>
              <label className="check-row">
                <input
                  type="checkbox"
                  data-testid="inject-periodic"
                  checked={periodic}
                  onChange={(e) => setPeriodic(e.target.checked)}
                />
                Periodic Loop
              </label>
              <label className="field">
                <span className="field-label">Interval (ms)</span>
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
                <span className="muted small">rolling_counter auto-increments each period</span>
              ) : null}
            </div>

            {preview?.warnings?.length ? (
              <p className="danger-text small" data-testid="inject-warnings" style={{ marginTop: 8 }}>
                Validation warnings: {preview.warnings.join(', ')}
              </p>
            ) : null}

            {/* Main Action Buttons */}
            <div className="actions tight" style={{ marginTop: 16 }}>
              <button
                type="button"
                className="primary"
                disabled={busy || !selected}
                data-testid="inject-submit"
                onClick={() => void doInject()}
              >
                {periodic ? 'Start Periodic' : 'Send Once'}
              </button>
              {activeJobs.length > 0 && (
                <button
                  type="button"
                  className="secondary danger-text"
                  disabled={busy}
                  data-testid="inject-stop-all"
                  onClick={() => void doStopAllJobs()}
                >
                  Stop All ({activeJobs.length})
                </button>
              )}
            </div>
          </section>

          {/* Side Panel: Started Controllers Manager & Templates */}
          <section className="panel inject-side" data-testid="inject-side-manager">
            {/* Started Controllers Sidebar */}
            <div className="panel-title-row">
              <h2>Started Controllers</h2>
              <span className="muted small">{startedList.length}</span>
            </div>
            <p className="muted small" style={{ marginTop: 0 }}>
              Active and saved transmission controllers. Click card to load settings.
            </p>

            {startedList.length === 0 ? (
              <p className="muted small" style={{ padding: '8px 0' }}>
                No controllers started yet. Use <strong>Start Periodic</strong> to register a controller here.
              </p>
            ) : (
              <div className="inject-template-list" style={{ marginBottom: 16 }}>
                {startedList.map((ctrl) => {
                  const isRunning = ctrl.status === 'RUNNING'
                  const msg = messages.find((m) => m.canonicalKey === ctrl.key)

                  return (
                    <div
                      key={ctrl.id}
                      className={`started-controller-card ${isRunning ? 'active-running' : 'active-stopped'}`}
                      onClick={() => loadControllerSettings(ctrl)}
                    >
                      <div className="started-controller-header">
                        <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
                          <span className="started-controller-title">{ctrl.name}</span>
                          <span className="mono muted small">{ctrl.can_id}</span>
                        </div>
                        <span className={`small mono ${isRunning ? 'ok-text' : 'warning-text'}`}>
                          {isRunning ? 'RUNNING' : 'STOPPED'}
                        </span>
                      </div>

                      <div className="started-controller-summary">
                        {formatSignalSummary(msg || null, ctrl.values)} · {ctrl.period_ms} ms
                      </div>

                      <div className="started-controller-actions" onClick={(e) => e.stopPropagation()}>
                        {isRunning ? (
                          <button
                            type="button"
                            className="secondary small danger-text"
                            disabled={busy}
                            onClick={() => void stopController(ctrl)}
                          >
                            Stop
                          </button>
                        ) : (
                          <button
                            type="button"
                            className="primary small"
                            disabled={busy}
                            onClick={() => void startController(ctrl)}
                          >
                            Start
                          </button>
                        )}
                        <button
                          type="button"
                          className="secondary small"
                          onClick={() => loadControllerSettings(ctrl)}
                        >
                          Load Settings
                        </button>
                      </div>
                    </div>
                  )
                })}
              </div>
            )}

            {/* Preset Templates */}
            <div className="panel-title-row" style={{ marginTop: 12 }}>
              <h2>Presets & Templates</h2>
              <span className="muted small">{TEMPLATES.length}</span>
            </div>
            <div className="inject-template-list" data-testid="inject-templates">
              {TEMPLATES.map((t) => (
                <button
                  key={t.id}
                  type="button"
                  className="inject-template-btn"
                  data-testid={`inject-template-${t.id}`}
                  onClick={() => applyTemplate(t)}
                >
                  <div style={{ display: 'flex', justifyContent: 'space-between', width: '100%' }}>
                    <strong>{t.label}</strong>
                    <span className="mono muted small">{t.bus.toUpperCase()}</span>
                  </div>
                  <span className="muted small">{t.description}</span>
                </button>
              ))}
            </div>

            <h3 style={{ marginTop: 18 }}>Live Value Map</h3>
            <dl className="kv compact" data-testid="inject-value-summary">
              {Object.entries(values).map(([k, v]) => (
                <FragmentPair key={k} k={k} v={v} />
              ))}
              {Object.keys(values).length === 0 && (
                <>
                  <dt>—</dt>
                  <dd className="muted">No signals configured</dd>
                </>
              )}
            </dl>
          </section>
        </div>
      ) : (
        /* Raw / Fault Injection Panel */
        <div className="inject-layout" data-testid="inject-raw-panel">
          <section className="panel inject-main">
            <h2>Raw / Fault Injection</h2>
            <p className="control-callout danger-text">
              Direct CAN frame generation. Bypasses signal codec validation. Use for protocol fault testing.
            </p>
            <div className="field-row" style={{ marginTop: 12 }}>
              <label className="field">
                <span className="field-label">Target Bus</span>
                <select
                  data-testid="raw-bus"
                  value={bus}
                  onChange={(e) => setBus(e.target.value as 'high' | 'low')}
                >
                  <option value="high">HIGH Bus</option>
                  <option value="low">LOW Bus</option>
                </select>
              </label>
              <label className="field">
                <span className="field-label">CAN ID (Hex format)</span>
                <input
                  data-testid="raw-can-id"
                  className="mono"
                  placeholder="e.g. 0x300 or 300"
                  value={rawId}
                  onChange={(e) => setRawId(e.target.value)}
                />
              </label>
            </div>

            <label className="field" style={{ marginTop: 10 }}>
              <span className="field-label">Payload Data Hex (0-8 bytes / even length hex)</span>
              <input
                data-testid="raw-data-hex"
                className="mono"
                placeholder="e.g. 00ffaabb11223344 or empty for DLC=0"
                value={rawHex}
                onChange={(e) => setRawHex(e.target.value)}
              />
            </label>

            <div style={{ marginTop: 8, fontSize: '13px' }}>
              Hex preview: <code className="mono">{formatBytes(rawHex)}</code>
            </div>

            <div style={{ marginTop: 12, display: 'flex', flexDirection: 'column', gap: 8 }}>
              <label className="check-row">
                <input
                  type="checkbox"
                  data-testid="raw-extended"
                  checked={rawExtended}
                  onChange={(e) => setRawExtended(e.target.checked)}
                />
                Extended CAN ID (29-bit)
              </label>

              <label className="check-row danger-text">
                <input
                  type="checkbox"
                  data-testid="raw-confirm"
                  checked={confirmRaw}
                  onChange={(e) => setConfirmRaw(e.target.checked)}
                />
                I confirm this is intentional raw / fault injection
              </label>
            </div>

            <div className="actions tight" style={{ marginTop: 16 }}>
              <button
                type="button"
                className="primary"
                disabled={busy}
                data-testid="raw-submit"
                onClick={() => void doRaw()}
              >
                Transmit Raw Frame
              </button>
            </div>
          </section>

          <section className="panel inject-side">
            <h2>Raw Fault Presets</h2>
            <p className="muted small">Quick-load common hardware/protocol failure payloads.</p>
            <div className="inject-template-list">
              {RAW_PRESETS.map((p) => (
                <button
                  key={p.id}
                  type="button"
                  className="inject-template-btn"
                  onClick={() => applyRawPreset(p)}
                >
                  <strong>{p.label}</strong>
                  <span className="muted small">{p.description}</span>
                </button>
              ))}
            </div>
          </section>
        </div>
      )}

      {/* Active Periodic Jobs Manager */}
      <section className="panel" style={{ marginTop: 16 }} data-testid="inject-active-jobs">
        <div className="panel-title-row">
          <h2>Active Periodic Transmitters</h2>
          <span className="mono muted small">{activeJobs.length} Running</span>
        </div>

        {activeJobs.length === 0 ? (
          <p className="muted small" style={{ margin: '8px 0' }}>
            No periodic injection loops currently running.
          </p>
        ) : (
          <div style={{ overflowX: 'auto', marginTop: 8 }}>
            <table className="data-table" style={{ width: '100%', borderCollapse: 'collapse' }}>
              <thead>
                <tr>
                  <th style={{ textAlign: 'left' }}>Job ID</th>
                  <th style={{ textAlign: 'left' }}>Bus</th>
                  <th style={{ textAlign: 'left' }}>Target Message</th>
                  <th style={{ textAlign: 'right' }}>Period</th>
                  <th style={{ textAlign: 'right' }}>Missed</th>
                  <th style={{ textAlign: 'left' }}>Last Result</th>
                  <th style={{ textAlign: 'center' }}>Action</th>
                </tr>
              </thead>
              <tbody>
                {activeJobs.map((job) => (
                  <tr key={job.job_id}>
                    <td className="mono small">{job.job_id}</td>
                    <td>
                      <span className="mono small">{job.bus.toUpperCase()}</span>
                    </td>
                    <td className="mono small">
                      {job.key || (job.can_id ? hexId(job.can_id) : '—')}
                    </td>
                    <td className="mono small" style={{ textAlign: 'right' }}>
                      {job.period_ms} ms
                    </td>
                    <td className="mono small" style={{ textAlign: 'right' }}>
                      {job.missed}
                    </td>
                    <td>
                      <span className={`small ${job.last_result === 'submitted' ? 'ok-text' : 'warning-text'}`}>
                        {job.last_result || 'active'}
                      </span>
                    </td>
                    <td style={{ textAlign: 'center' }}>
                      <button
                        type="button"
                        className="secondary small danger-text"
                        disabled={busy}
                        onClick={() => void doStopJob(job.job_id)}
                      >
                        Stop
                      </button>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </section>

      {/* Command Acks & Transmit Log Panel */}
      <section className="panel" style={{ marginTop: 16 }} data-testid="inject-ack-log">
        <div className="panel-title-row">
          <h2>Command Acks & Transmission History</h2>
          {ackLogs.length > 0 && (
            <button
              type="button"
              className="secondary small"
              onClick={() => setAckLogs([])}
            >
              Clear Log
            </button>
          )}
        </div>

        {log ? (
          <div className="control-callout" style={{ margin: '8px 0', fontSize: '13px' }}>
            Latest: <code className="mono">{log}</code>
          </div>
        ) : null}

        {ackLogs.length === 0 ? (
          <p className="muted small" style={{ margin: '8px 0' }}>
            No recent command transmissions recorded.
          </p>
        ) : (
          <div style={{ maxHeight: '240px', overflowY: 'auto', marginTop: 8 }}>
            <table className="data-table" style={{ width: '100%', borderCollapse: 'collapse' }}>
              <thead>
                <tr>
                  <th style={{ textAlign: 'left' }}>Time</th>
                  <th style={{ textAlign: 'left' }}>Type</th>
                  <th style={{ textAlign: 'left' }}>Bus</th>
                  <th style={{ textAlign: 'left' }}>Message</th>
                  <th style={{ textAlign: 'left' }}>Payload</th>
                  <th style={{ textAlign: 'left' }}>Status</th>
                </tr>
              </thead>
              <tbody>
                {ackLogs.map((ack) => (
                  <tr key={ack.id}>
                    <td className="mono small muted">{ack.timestamp}</td>
                    <td>
                      <span className="mono small">{ack.type}</span>
                    </td>
                    <td className="mono small">{ack.bus.toUpperCase()}</td>
                    <td className="mono small">
                      {ack.can_id} {ack.name}
                    </td>
                    <td className="mono small">{formatBytes(ack.data_hex)}</td>
                    <td className={ack.ok ? 'ok-text small' : 'danger-text small'}>
                      {ack.detail}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </section>
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
