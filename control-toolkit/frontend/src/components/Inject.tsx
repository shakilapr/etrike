/**
 * CAN Injector — dense editor. Active TX lives in the global right rail (App).
 */
import { useCallback, useEffect, useMemo, useState } from 'react'
import { api } from '../api'
import { hexId } from '../lib/format'
import { useActiveTxStore } from '../lib/activeTxStore'
import { useAppStore } from '../store'
import type { DictField, DictMessage } from './CanDictionary'
import { NumericDraft } from './NumericDraft'
import {
  Button,
  Input,
  Panel,
  Seg,
  SegButton,
  Toolbar,
  ToolbarDivider,
  ToolbarGroup,
  ToolbarItem,
} from './ui'
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
    { label: '0', value: 0 },
    { label: '0.8', value: 800 },
    { label: '2', value: 2000 },
    { label: '3', value: 3000 },
  ],
  motor_speed_mmps: [
    { label: '0', value: 0 },
    { label: '2', value: 2000 },
    { label: '3', value: 3000 },
  ],
  yaw_rate_mrad_s: [
    { label: 'L', value: -1500 },
    { label: '0', value: 0 },
    { label: 'R', value: 1500 },
  ],
  gear: [
    { label: 'N', value: 0 },
    { label: 'D', value: 1 },
    { label: 'S', value: 2 },
    { label: 'R', value: 3 },
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
    description: '0x300 with invalid 0xFF payload',
    bus: 'high',
    can_id: '0x300',
    data_hex: 'ffffffffffffffff',
  },
  {
    id: 'dlc-mismatch',
    label: 'DLC Mismatch (Short)',
    description: '0x300 with 4-byte payload',
    bus: 'high',
    can_id: '0x300',
    data_hex: '00000000',
  },
  {
    id: 'unmapped-id',
    label: 'Unmapped CAN ID (0x7FF)',
    description: 'Unmapped diagnostic ID',
    bus: 'high',
    can_id: '0x7FF',
    data_hex: '11223344',
  },
  {
    id: 'estop-raw',
    label: 'Raw ESTOP (0x001)',
    description: 'Empty DLC 0 safety frame',
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

/**
 * Per-ECU wire direction (not injectability):
 *  - Output = this ECU is the sender (it publishes the frame)
 *  - Input  = this ECU is a receiver (it consumes the frame)
 * Input and output sets for the same ECU are different messages.
 */
type EcuDir = 'sends' | 'receives' | 'both'

const ECU_ORDER = [
  'Host',
  'RT',
  'SYS',
  'MTR',
  'HMI',
  'EPS_C',
  'SEB',
  'Any',
] as const

const ECU_LABEL: Record<string, string> = {
  Host: 'Host',
  RT: 'RT',
  SYS: 'SYS',
  MTR: 'MTR',
  HMI: 'HMI',
  EPS_C: 'EPS_C (SBW)',
  SEB: 'SEB (BBW)',
  Any: 'Any',
}

function ecuRank(name: string): number {
  const i = (ECU_ORDER as readonly string[]).indexOf(name)
  return i >= 0 ? i : 100
}

function msgSender(m: DictMessage): string {
  const s = (m.sender || '').trim()
  return s && s !== '—' ? s : '—'
}

function msgReceivers(m: DictMessage): string[] {
  return (m.receivers || [])
    .map((r) => String(r).trim())
    .filter((r) => r && r !== '—')
}

function ecuSends(m: DictMessage, ecu: string): boolean {
  return msgSender(m) === ecu
}

function ecuReceives(m: DictMessage, ecu: string): boolean {
  return msgReceivers(m).includes(ecu)
}

/** Does this frame belong on the ECU list under the chosen direction? */
function matchesEcuDir(m: DictMessage, ecu: string, dir: EcuDir): boolean {
  if (ecu === 'all') return true
  const out = ecuSends(m, ecu)
  const inn = ecuReceives(m, ecu)
  if (dir === 'sends') return out
  if (dir === 'receives') return inn
  return out || inn
}

function routeLabel(m: DictMessage): string {
  const from = msgSender(m)
  const to = msgReceivers(m)
  if (to.length) return `${from} → ${to.join(',')}`
  return from
}

/** Named inject can build a payload (independent of ECU input/output). */
function canNamedInject(m: DictMessage): boolean {
  const caps = m.capabilities || {}
  if (caps.decodedInjection === true) return true
  if (caps.decodedInjection === false) return false
  const strategy = String(caps.codecStrategy || '')
  if (strategy === 'generated' || strategy === '') return true
  if ((m.fields || []).length === 0 || m.dlc === 0) return true
  const k = m.canonicalKey
  return k === 'ses:vcu_ses_req' || k === 'seb:vcu_seb_req'
}

export function Inject() {
  const status = useAppStore((s) => s.status)
  const setStatus = useAppStore((s) => s.setStatus)
  const refreshJobs = useActiveTxStore((s) => s.refreshJobs)
  const stopAllTx = useActiveTxStore((s) => s.stopAll)
  const activeJobCount = useActiveTxStore((s) => s.jobs.length + s.paused.length)
  const [messages, setMessages] = useState<DictMessage[]>([])
  const [mode, setMode] = useState<'named' | 'raw'>('named')
  const [bus, setBus] = useState<'high' | 'low'>('high')
  /** Which ECU; "all" = every message on the bus. */
  const [ecuFilter, setEcuFilter] = useState<string>('all')
  /** Relative to selected ECU: frames it sends vs receives (ignored when ECU=all). */
  const [ecuDir, setEcuDir] = useState<EcuDir>('both')
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

  const [ackLogs, setAckLogs] = useState<AckLog[]>([])
  const [templatesOpen, setTemplatesOpen] = useState(true)
  const [logOpen, setLogOpen] = useState(false)

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
    setMessages((d.messages || []) as DictMessage[])
  }, [])

  useEffect(() => {
    void loadCatalog().catch((e) => setLog(String(e)))
  }, [loadCatalog])

  // Collapse templates once something is running
  useEffect(() => {
    if (activeJobCount > 0) setTemplatesOpen(false)
  }, [activeJobCount])

  const onBus = useMemo(
    () => messages.filter((m) => m.bus === bus),
    [messages, bus],
  )

  // Every ECU that appears as sender or receiver on this bus.
  const ecuOptions = useMemo(() => {
    const set = new Set<string>()
    for (const m of onBus) {
      const s = msgSender(m)
      if (s !== '—') set.add(s)
      for (const r of msgReceivers(m)) set.add(r)
    }
    return [...set].sort((a, b) => ecuRank(a) - ecuRank(b) || a.localeCompare(b))
  }, [onBus])

  const busMessages = useMemo(() => {
    const q = filter.trim().toLowerCase()
    return onBus
      .filter((m) => matchesEcuDir(m, ecuFilter, ecuDir))
      .filter((m) => {
        if (!q) return true
        const route = routeLabel(m).toLowerCase()
        return (
          m.name.toLowerCase().includes(q) ||
          m.id.toLowerCase().includes(q) ||
          m.canonicalKey.toLowerCase().includes(q) ||
          route.includes(q) ||
          msgSender(m).toLowerCase().includes(q)
        )
      })
      .sort((a, b) => {
        // When an ECU is selected: its outputs first, then its inputs.
        if (ecuFilter !== 'all') {
          const aOut = ecuSends(a, ecuFilter) ? 0 : 1
          const bOut = ecuSends(b, ecuFilter) ? 0 : 1
          if (aOut !== bOut) return aOut - bOut
        } else {
          const sa = ecuRank(msgSender(a)) - ecuRank(msgSender(b))
          if (sa !== 0) return sa
        }
        return a.can_id - b.can_id || a.name.localeCompare(b.name)
      })
  }, [onBus, ecuFilter, ecuDir, filter])

  /**
   * Message groups for the select:
   *  - ECU selected → Outputs (sends) / Inputs (receives)
   *  - All ECUs → group by sender
   */
  const messageGroups = useMemo(() => {
    if (ecuFilter !== 'all') {
      const outs = busMessages.filter((m) => ecuSends(m, ecuFilter))
      const inns = busMessages.filter((m) => ecuReceives(m, ecuFilter) && !ecuSends(m, ecuFilter))
      const groups: Array<{ key: string; label: string; items: DictMessage[] }> = []
      if (outs.length) {
        groups.push({
          key: 'out',
          label: `Outputs · ${ECU_LABEL[ecuFilter] ?? ecuFilter} sends · ${outs.length}`,
          items: outs,
        })
      }
      if (inns.length) {
        groups.push({
          key: 'in',
          label: `Inputs · ${ECU_LABEL[ecuFilter] ?? ecuFilter} receives · ${inns.length}`,
          items: inns,
        })
      }
      return groups
    }
    const map = new Map<string, DictMessage[]>()
    for (const m of busMessages) {
      const s = msgSender(m)
      const list = map.get(s) || []
      list.push(m)
      map.set(s, list)
    }
    return [...map.entries()]
      .sort((a, b) => ecuRank(a[0]) - ecuRank(b[0]) || a[0].localeCompare(b[0]))
      .map(([sender, items]) => ({
        key: sender,
        label: `Sender · ${ECU_LABEL[sender] ?? sender} · ${items.length}`,
        items,
      }))
  }, [busMessages, ecuFilter])

  const selected = useMemo(() => {
    return (
      busMessages.find((m) => m.canonicalKey === selectedKey) ||
      busMessages.find((m) => `${m.bus}:${m.id}` === selectedKey) ||
      busMessages[0] ||
      null
    )
  }, [busMessages, selectedKey])

  const selectedInjectable = selected ? canNamedInject(selected) : false

  const sendCount =
    ecuFilter === 'all'
      ? onBus.length
      : onBus.filter((m) => ecuSends(m, ecuFilter)).length
  const recvCount =
    ecuFilter === 'all' ? 0 : onBus.filter((m) => ecuReceives(m, ecuFilter)).length

  // Keep selection valid when filters change
  useEffect(() => {
    if (!selectedKey) return
    if (busMessages.some((m) => m.canonicalKey === selectedKey)) return
    const next = busMessages[0]
    if (next) {
      setSelectedKey(next.canonicalKey)
      setValues(defaultsFor(next))
    } else {
      setSelectedKey('')
      setValues({})
    }
  }, [busMessages, selectedKey])

  const handleBusChange = (newBus: 'high' | 'low') => {
    setBus(newBus)
    setEcuFilter('all')
    setEcuDir('both')
    const first = messages.find((m) => m.bus === newBus)
    if (first) {
      setSelectedKey(first.canonicalKey)
      setValues(defaultsFor(first))
    } else {
      setSelectedKey('')
      setValues({})
    }
    setConfirmEstop(false)
  }

  const handleEcuChange = (ecu: string) => {
    setEcuFilter(ecu)
    setEcuDir('both')
  }

  const handleMessageChange = (key: string) => {
    setSelectedKey(key)
    const msg = busMessages.find((m) => m.canonicalKey === key)
    if (msg) setValues(defaultsFor(msg))
    setConfirmEstop(false)
  }

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
    setLog(`Template: ${t.label}`)
  }

  function applyRawPreset(p: RawPreset) {
    setMode('raw')
    setBus(p.bus)
    setRawId(p.can_id)
    setRawHex(p.data_hex)
    setRawExtended(Boolean(p.is_extended))
    setConfirmRaw(true)
    setLog(`Raw preset: ${p.label}`)
  }

  async function ensureSessionAndTx() {
    const st = await api.status()
    if (!st.session?.session_id) {
      throw new Error('No active session. Start a session in Settings first.')
    }
    if (st.session.bench_tx !== 'enabled') {
      throw new Error('Bench TX is disabled. Arm Bench TX before injecting.')
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
        detail: 'Bench TX armed',
      })
      setLog('Bench TX armed.')
    } catch (e) {
      setLog(`Arm failed: ${String(e)}`)
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
        throw new Error('Confirm ESTOP injection before sending.')
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
        setLog(`Periodic ${selected.name} @ ${r.period_ms} ms [${r.job_id}]`)
        addAckLog({
          timestamp,
          type: 'PERIODIC',
          bus: selected.bus,
          can_id: hexText,
          name: selected.name,
          data_hex: dataHex,
          ok: true,
          detail: `Periodic @ ${r.period_ms} ms`,
        })
        await refreshJobs()
      } else {
        setLog(`Injected ${selected.name} · ${formatBytes(dataHex)}`)
        addAckLog({
          timestamp,
          type: 'ONESHOT',
          bus: selected.bus,
          can_id: hexText,
          name: selected.name,
          data_hex: dataHex,
          ok: true,
          detail: 'One-shot ok',
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

  async function doStopAllJobs() {
    setBusy(true)
    const timestamp = new Date().toLocaleTimeString()
    try {
      await stopAllTx()
      const count = useActiveTxStore.getState().jobs.length
      setLog('Cleared host TX jobs')
      addAckLog({
        timestamp,
        type: 'STOP',
        bus: 'system',
        can_id: '—',
        name: 'CANCEL_ALL',
        data_hex: '',
        ok: true,
        detail: count === 0 ? 'All cleared' : `Remaining ${count}`,
      })
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
        throw new Error('Confirm raw injection before sending.')
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
      setLog(`Raw ${formattedId} dlc=${r.dlc}`)
      addAckLog({
        timestamp,
        type: 'RAW',
        bus,
        can_id: formattedId,
        name: 'RAW_FRAME',
        data_hex: r.data_hex,
        ok: true,
        detail: `dlc=${r.dlc}`,
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

  const previewHex = mode === 'named' ? preview?.data_hex ?? '' : rawHex
  const wireText = formatBytes(previewHex)

  return (
    <WorkspaceShell
      testId="workspace-inject"
      className="inject-workspace"
      title="Inject"
      description="Named or raw CAN inject · host TX is managed in the shared Active TX rail"
    >
      <Toolbar data-testid="inject-gate">
        <ToolbarGroup>
          <ToolbarItem label="TX">
            <strong className={benchOn ? 'ok-text' : 'danger-text'} data-testid="inject-bench-tx">
              {benchOn ? 'Armed' : 'Off'}
            </strong>
          </ToolbarItem>
          {!benchOn && (
            <Button
              size="sm"
              disabled={busy || !sessionId}
              data-testid="inject-arm-tx"
              onClick={() => void enableBenchTx()}
            >
              Arm TX
            </Button>
          )}
          <ToolbarDivider />
          <Seg role="tablist" aria-label="Inject mode">
            <SegButton
              role="tab"
              active={mode === 'named'}
              data-testid="inject-mode-named"
              onClick={() => setMode('named')}
            >
              Named
            </SegButton>
            <SegButton
              role="tab"
              active={mode === 'raw'}
              data-testid="inject-mode-raw"
              onClick={() => setMode('raw')}
            >
              Raw
            </SegButton>
          </Seg>
        </ToolbarGroup>
        <ToolbarGroup>
          <ToolbarItem label="Wire" className="max-w-[min(420px,50vw)]">
            <strong className="mono text-primary text-[11px] truncate" data-testid="inject-preview-hex">
              {wireText}
            </strong>
          </ToolbarItem>
          {activeJobCount > 0 && (
            <Button
              variant="secondary"
              size="sm"
              className="danger-text"
              disabled={busy}
              data-testid="inject-toolbar-stop-all"
              title="Remove all active and paused TX"
              onClick={() => void doStopAllJobs()}
            >
              Clear all ({activeJobCount})
            </Button>
          )}
        </ToolbarGroup>
      </Toolbar>

      {mode === 'named' ? (
        <div className="inject-layout" data-testid="inject-named-panel">
          <Panel className="inject-main">
            <div className="inject-editor-bar" data-testid="inject-editor-bar">
              <Seg data-testid="inject-bus-tabs">
                {(['high', 'low'] as const).map((b) => (
                  <SegButton
                    key={b}
                    active={bus === b}
                    data-testid={`inject-bus-${b}`}
                    onClick={() => handleBusChange(b)}
                  >
                    {b === 'high' ? 'High' : 'Low'}
                  </SegButton>
                ))}
              </Seg>
              <select
                className="inject-ecu-select"
                data-testid="inject-ecu-filter"
                value={ecuFilter}
                aria-label="ECU"
                title="Filter by ECU. Outputs = this ECU sends; inputs = this ECU receives."
                onChange={(e) => handleEcuChange(e.target.value)}
              >
                <option value="all">All ECUs ({onBus.length})</option>
                {ecuOptions.map((ecu) => {
                  const nSend = onBus.filter((m) => ecuSends(m, ecu)).length
                  const nRecv = onBus.filter((m) => ecuReceives(m, ecu)).length
                  return (
                    <option key={ecu} value={ecu}>
                      {ECU_LABEL[ecu] ?? ecu} · out {nSend} / in {nRecv}
                    </option>
                  )
                })}
              </select>
              <Seg
                data-testid="inject-ecu-dir"
                aria-label="Direction relative to selected ECU"
                title={
                  ecuFilter === 'all'
                    ? 'Pick an ECU to filter its outputs vs inputs'
                    : `Relative to ${ecuFilter}: messages it sends (outputs) vs receives (inputs)`
                }
              >
                <SegButton
                  active={ecuDir === 'both'}
                  disabled={ecuFilter === 'all'}
                  data-testid="inject-ecu-dir-both"
                  onClick={() => setEcuDir('both')}
                >
                  Both
                  {ecuFilter !== 'all' ? ` (${sendCount + recvCount})` : ''}
                </SegButton>
                <SegButton
                  active={ecuDir === 'sends'}
                  disabled={ecuFilter === 'all'}
                  data-testid="inject-ecu-dir-sends"
                  onClick={() => setEcuDir('sends')}
                >
                  Sends{ecuFilter !== 'all' ? ` (${sendCount})` : ''}
                </SegButton>
                <SegButton
                  active={ecuDir === 'receives'}
                  disabled={ecuFilter === 'all'}
                  data-testid="inject-ecu-dir-receives"
                  onClick={() => setEcuDir('receives')}
                >
                  Receives{ecuFilter !== 'all' ? ` (${recvCount})` : ''}
                </SegButton>
              </Seg>
              <Input
                className="inject-filter-input h-8 min-h-8 max-w-none"
                data-testid="inject-filter"
                placeholder="Filter name / id / route…"
                value={filter}
                onChange={(e) => setFilter(e.target.value)}
              />
              <select
                className="inject-message-select"
                data-testid="inject-message"
                value={selected?.canonicalKey ?? ''}
                onChange={(e) => handleMessageChange(e.target.value)}
              >
                {busMessages.length === 0 && (
                  <option value="">
                    No messages
                    {ecuFilter !== 'all' ? ` for ${ecuFilter}` : ''} on {bus}
                  </option>
                )}
                {messageGroups.map((g) => (
                  <optgroup key={g.key} label={g.label}>
                    {g.items.map((m) => {
                      const inj = canNamedInject(m)
                      const roleNote =
                        ecuFilter !== 'all'
                          ? ecuSends(m, ecuFilter)
                            ? ' · out'
                            : ' · in'
                          : ''
                      return (
                        <option
                          key={`${m.bus}-${m.canonicalKey}-${m.can_id}`}
                          value={m.canonicalKey}
                        >
                          {m.id} {m.name}
                          {' · '}
                          {routeLabel(m)}
                          {roleNote}
                          {(m.fields || []).length === 0 ? ' · DLC0' : ''}
                          {inj ? '' : ' · no encode'}
                        </option>
                      )
                    })}
                  </optgroup>
                ))}
              </select>
            </div>

            {selected ? (
              <p className="muted small inject-msg-meta" data-testid="inject-msg-meta">
                <span className="mono">{selected.id}</span>
                {' · '}
                <strong>{selected.name}</strong>
                {' · '}
                <span className="mono" title="Wire route: sender → receivers">
                  {routeLabel(selected)}
                </span>
                {ecuFilter !== 'all' ? (
                  <>
                    {' · for '}
                    <span className="mono">{ECU_LABEL[ecuFilter] ?? ecuFilter}</span>
                    {': '}
                    {ecuSends(selected, ecuFilter) ? (
                      <span className="tx-text">output (sends)</span>
                    ) : ecuReceives(selected, ecuFilter) ? (
                      <span>input (receives)</span>
                    ) : null}
                  </>
                ) : (
                  <>
                    {' · sender '}
                    <span className="mono">{msgSender(selected)}</span>
                  </>
                )}
                {' · '}
                {selectedInjectable ? (
                  <span className="tx-text">named inject OK</span>
                ) : (
                  <span className="muted" title="No named encoder — use Raw for fault injection">
                    no named encode
                  </span>
                )}
                {' · '}
                <span className="mono muted">{busMessages.length} listed</span>
              </p>
            ) : null}

            {selected && (selected.fields || []).length > 0 ? (
              <div className="form-grid inject-fields" data-testid="inject-fields">
                {selected.fields.map((field) => {
                  const presets = SIGNAL_PRESETS[field.key] || []
                  const curVal = Number(values[field.key] ?? 0)
                  const hasSlider =
                    field.kind !== 'boolean' &&
                    field.kind !== 'enum' &&
                    field.min != null &&
                    field.max != null

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
                        <label className="check-row">
                          <input
                            type="checkbox"
                            data-testid={`inject-val-${field.key}`}
                            checked={Boolean(values[field.key])}
                            onChange={(e) => setField(field.key, e.target.checked)}
                          />
                          <span className={`small ${values[field.key] ? 'ok-text' : 'muted'}`}>
                            {values[field.key] ? '1' : '0'}
                          </span>
                        </label>
                      ) : field.kind === 'enum' && field.options?.length ? (
                        <select
                          data-testid={`inject-val-${field.key}`}
                          value={String(values[field.key] ?? field.options[0]?.value ?? 0)}
                          onChange={(e) => {
                            const raw = e.target.value
                            const asNum = Number(raw)
                            setField(
                              field.key,
                              Number.isFinite(asNum) ? asNum : (raw as unknown as number),
                            )
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
                            <div style={{ width: hasSlider ? '88px' : '100%' }}>
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
              <p className="muted small" data-testid="inject-no-fields">
                No payload fields (DLC 0 / event).
              </p>
            ) : null}

            {isEstop && (
              <label className="check-row danger-text" data-testid="inject-confirm-estop">
                <input
                  type="checkbox"
                  checked={confirmEstop}
                  onChange={(e) => setConfirmEstop(e.target.checked)}
                />
                Confirm ESTOP inject
              </label>
            )}

            <div className="inject-actions-row">
              <label className="check-row">
                <input
                  type="checkbox"
                  data-testid="inject-periodic"
                  checked={periodic}
                  onChange={(e) => setPeriodic(e.target.checked)}
                />
                Periodic
              </label>
              <label className="field inject-period-field">
                <span className="field-label">ms</span>
                <NumericDraft
                  testId="inject-period"
                  value={periodMs}
                  min={1}
                  max={60000}
                  disabled={!periodic}
                  onValue={setPeriodMs}
                />
              </label>
              <Button
                disabled={busy || !selected || !selectedInjectable}
                data-testid="inject-submit"
                title={
                  selectedInjectable
                    ? undefined
                    : 'This frame has no named encoder (RX status/telemetry). Use Raw mode or inject a command frame.'
                }
                onClick={() => void doInject()}
              >
                {periodic ? 'Start loop' : 'Send once'}
              </Button>
            </div>

            {preview?.warnings?.length ? (
              <p className="danger-text small" data-testid="inject-warnings">
                {preview.warnings.join(', ')}
              </p>
            ) : null}
            {log ? (
              <p className="muted small mono inject-status-line" data-testid="inject-status-line">
                {log}
              </p>
            ) : null}
          </Panel>
        </div>
      ) : (
        <div className="inject-layout" data-testid="inject-raw-panel">
          <Panel className="inject-main">
            <p className="control-callout danger-text" style={{ marginTop: 0 }}>
              Raw frames bypass signal validation — for fault testing only.
            </p>
            <div className="field-row">
              <label className="field">
                <span className="field-label">Bus</span>
                <select
                  data-testid="raw-bus"
                  value={bus}
                  onChange={(e) => setBus(e.target.value as 'high' | 'low')}
                >
                  <option value="high">High</option>
                  <option value="low">Low</option>
                </select>
              </label>
              <label className="field">
                <span className="field-label">CAN ID</span>
                <input
                  data-testid="raw-can-id"
                  className="mono"
                  placeholder="0x300"
                  value={rawId}
                  onChange={(e) => setRawId(e.target.value)}
                />
              </label>
            </div>
            <label className="field" style={{ marginTop: 8 }}>
              <span className="field-label">Data hex</span>
              <input
                data-testid="raw-data-hex"
                className="mono"
                placeholder="even hex or empty for DLC 0"
                value={rawHex}
                onChange={(e) => setRawHex(e.target.value)}
              />
            </label>
            <div className="inject-raw-checks">
              <label className="check-row">
                <input
                  type="checkbox"
                  data-testid="raw-extended"
                  checked={rawExtended}
                  onChange={(e) => setRawExtended(e.target.checked)}
                />
                Extended ID
              </label>
              <label className="check-row danger-text">
                <input
                  type="checkbox"
                  data-testid="raw-confirm"
                  checked={confirmRaw}
                  onChange={(e) => setConfirmRaw(e.target.checked)}
                />
                Confirm raw inject
              </label>
            </div>
            <div className="actions tight" style={{ marginTop: 12 }}>
              <Button disabled={busy} data-testid="raw-submit" onClick={() => void doRaw()}>
                Transmit raw
              </Button>
            </div>
            {log ? (
              <p className="muted small mono inject-status-line">{log}</p>
            ) : null}
          </Panel>
        </div>
      )}

      {/* Collapsible templates */}
      <Panel className="inject-collapse" data-testid="inject-templates-section">
        <button
          type="button"
          className="inject-collapse-toggle"
          data-testid="inject-templates-toggle"
          aria-expanded={templatesOpen}
          onClick={() => setTemplatesOpen((o) => !o)}
        >
          <span>
            {mode === 'raw' ? 'Fault presets' : 'Templates'}{' '}
            <span className="muted small">
              ({mode === 'raw' ? RAW_PRESETS.length : TEMPLATES.length})
            </span>
          </span>
          <span className="muted small">{templatesOpen ? '▾' : '▸'}</span>
        </button>
        {templatesOpen && (
          <div
            className="inject-template-grid"
            data-testid="inject-templates"
          >
            {mode === 'named'
              ? TEMPLATES.map((t) => (
                  <button
                    key={t.id}
                    type="button"
                    className="inject-template-chip"
                    data-testid={`inject-template-${t.id}`}
                    onClick={() => applyTemplate(t)}
                  >
                    <span className="inject-template-chip-label">{t.label}</span>
                    <span className="mono muted small">{t.bus === 'high' ? 'H' : 'L'}</span>
                  </button>
                ))
              : RAW_PRESETS.map((p) => (
                  <button
                    key={p.id}
                    type="button"
                    className="inject-template-chip"
                    onClick={() => applyRawPreset(p)}
                  >
                    <span className="inject-template-chip-label">{p.label}</span>
                    <span className="mono muted small">{p.bus === 'high' ? 'H' : 'L'}</span>
                  </button>
                ))}
          </div>
        )}
      </Panel>

      {/* Collapsible transmit log */}
      <Panel className="inject-collapse" data-testid="inject-ack-log">
        <button
          type="button"
          className="inject-collapse-toggle"
          data-testid="inject-log-toggle"
          aria-expanded={logOpen}
          onClick={() => setLogOpen((o) => !o)}
        >
          <span>
            Transmit log{' '}
            <span className="muted small">({ackLogs.length})</span>
          </span>
          <span className="muted small">{logOpen ? '▾' : '▸'}</span>
        </button>
        {logOpen && (
          <div className="inject-log-body">
            {ackLogs.length > 0 && (
              <Button variant="secondary" size="sm" onClick={() => setAckLogs([])}>
                Clear
              </Button>
            )}
            {ackLogs.length === 0 ? (
              <p className="muted small">No transmissions yet.</p>
            ) : (
              <table className="data-table compact inject-log-table">
                <thead>
                  <tr>
                    <th>Time</th>
                    <th>Type</th>
                    <th>Bus</th>
                    <th>Msg</th>
                    <th>Status</th>
                  </tr>
                </thead>
                <tbody>
                  {ackLogs.map((ack) => (
                    <tr key={ack.id}>
                      <td className="mono small muted">{ack.timestamp}</td>
                      <td className="mono small">{ack.type}</td>
                      <td className="mono small">{ack.bus}</td>
                      <td className="mono small">
                        {ack.can_id} {ack.name}
                      </td>
                      <td className={ack.ok ? 'ok-text small' : 'danger-text small'}>
                        {ack.detail}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}
          </div>
        )}
      </Panel>
    </WorkspaceShell>
  )
}
