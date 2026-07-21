/**
 * One-shot splitter: App.tsx → component modules.
 * Run from frontend/: node scripts/split-app.mjs
 */
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const root = path.resolve(__dirname, '..')
const appPath = path.join(root, 'src', 'App.tsx')
const lines = fs.readFileSync(appPath, 'utf8').split(/\r?\n/)

function slice(a, b) {
  // 1-based inclusive
  return lines.slice(a - 1, b).join('\n')
}

function write(rel, content) {
  const p = path.join(root, rel)
  fs.mkdirSync(path.dirname(p), { recursive: true })
  fs.writeFileSync(p, content.replace(/\n+$/, '') + '\n', 'utf8')
  console.log('wrote', rel, `(${content.split('\n').length} lines)`)
}

// ── shared helpers ──────────────────────────────────────────────────
write(
  'src/lib/signals.ts',
  `import type { MessageState } from '../store'

export const PROFILE_LABELS: Record<string, string> = {
  pure_software: 'Computer · Virtual',
  bench_test: 'Real · CANalyst Bench',
  full_vehicle: 'Real · CANalyst Vehicle',
}

/** Session profile → transport mode shown in Settings toggle. */
export function transportModeOf(profile: string | undefined | null): 'computer' | 'real' {
  if (profile === 'bench_test' || profile === 'full_vehicle') return 'real'
  return 'computer'
}

export function signalText(m: MessageState | undefined, key: string): string {
  if (!m?.signals?.[key]) return '—'
  const s = m.signals[key]
  return String(s.enum_label ?? s.engineering_value ?? '—')
}

/** Empty/null/whitespace → em dash (topbar mode/power often arrives as ""). */
export function dash(v: unknown): string {
  if (v == null) return '—'
  const s = String(v).trim()
  return s === '' ? '—' : s
}

/** Req/Conf line without "— · —" style doubling when both empty. */
export function formatReqConf(req: unknown, conf: unknown): string {
  const r = dash(req)
  const c = dash(conf)
  if (r === '—' && c === '—') return '—'
  if (r === c) return r
  return \`Req \${r} · Conf \${c}\`
}

export function signalNum(m: MessageState | undefined, key: string): number | null {
  const v = m?.signals?.[key]?.engineering_value
  if (typeof v === 'number' && Number.isFinite(v)) return v
  if (typeof v === 'string' && v.trim() !== '' && !Number.isNaN(Number(v))) return Number(v)
  return null
}

export function findMsg(messages: MessageState[], name: string, bus?: string) {
  return messages.find((m) => m.name === name && (bus == null || m.bus === bus))
}

export type OverallHealth = 'healthy' | 'degraded' | 'fault' | 'offline'

export function busActivityTone(activity?: string): 'ok' | 'warn' | 'muted' | 'danger' {
  const a = (activity || '').toLowerCase()
  if (a === 'active' || a === 'rx' || a === 'tx' || a === 'live') return 'ok'
  if (a === 'idle' || a === 'quiet') return 'warn'
  if (a === 'error' || a === 'fault' || a === 'overflow') return 'danger'
  return 'muted' // unseen / —
}

export function shortHash(h: string | null | undefined, n = 12): string {
  if (!h) return '—'
  return h.length > n ? \`\${h.slice(0, n)}…\` : h
}
`,
)

write(
  'src/lib/session.ts',
  `import { api } from '../api'
import type { Status } from '../store'

export async function activateTransportProfile(profile: string): Promise<Status> {
  if (profile === 'bench_test' || profile === 'full_vehicle') {
    const profiles = await api.profiles()
    if (!profiles.physical_adapter?.available) {
      throw new Error(
        profiles.physical_adapter?.reason ||
          'CANalyst-II is unavailable. Connect USB and retry; Computer remains active.',
      )
    }
  }
  const current = await api.status()
  if (current.session?.session_id && current.session.profile !== profile) {
    await api.changeProfile(
      current.session.session_id,
      profile,
      current.session.revision,
      true,
    )
  } else if (!current.session?.session_id) {
    await api.createSession(profile)
  }
  return api.status()
}
`,
)

// ── icons (types + components + NAV_SECTIONS) ───────────────────────
const iconsBody = slice(22, 242) // NavItem types through NAV_SECTIONS
write(
  'src/components/icons.tsx',
  `import type { ReactNode } from 'react'
import type { Workspace } from '../store'

${iconsBody.replace(/^function /gm, 'export function ').replace(/^const NAV_SECTIONS/, 'export const NAV_SECTIONS')}
`,
)

// ── primitives ──────────────────────────────────────────────────────
const liveness = slice(256, 260)
const meter = slice(291, 357)
const metric = slice(359, 423)
write(
  'src/components/primitives.tsx',
  `import { FreshnessBadge } from './FreshnessBadge'

${liveness.replace('function LivenessBadge', 'export function LivenessBadge')}

${meter.replace('function MeterBar', 'export function MeterBar').replace('function StatusPill', 'export function StatusPill')}

${metric.replace('function MetricCard', 'export function MetricCard')}
`,
)

// ── NumericDraft ────────────────────────────────────────────────────
write(
  'src/components/NumericDraft.tsx',
  `import { useEffect, useState } from 'react'

${slice(2656, 2712).replace('function NumericDraft', 'export function NumericDraft')}
`,
)

// ── Topbar ──────────────────────────────────────────────────────────
write(
  'src/components/Topbar.tsx',
  `import { useState } from 'react'
import { api } from '../api'
import { busActivityTone, dash, PROFILE_LABELS, transportModeOf, type OverallHealth } from '../lib/signals'
import { useAppStore } from '../store'
import { IconCable, IconExternalLink, IconMonitor } from './icons'
import { StatusPill } from './primitives'

${slice(441, 766).replace('function Topbar', 'export function Topbar')}
`,
)

// ── Sidebar ─────────────────────────────────────────────────────────
write(
  'src/components/Sidebar.tsx',
  `import { findMsg } from '../lib/signals'
import { useAppStore } from '../store'
import { NAV_SECTIONS } from './icons'

${slice(768, 950).replace('function Sidebar', 'export function Sidebar')}
`,
)

// ── Overview ────────────────────────────────────────────────────────
write(
  'src/components/Overview.tsx',
  `import { formatReqConf, findMsg, signalNum, signalText } from '../lib/signals'
import { useAppStore } from '../store'
import { FreshnessBadge } from './FreshnessBadge'
import { MeterBar, MetricCard, StatusPill } from './primitives'

${slice(954, 1304).replace('function Overview', 'export function Overview')}
`,
)

// ── Network ─────────────────────────────────────────────────────────
write(
  'src/components/Network.tsx',
  `import { hexId } from '../lib/format'
import { useAppStore, type TopologyNode } from '../store'
import { LivenessBadge } from './primitives'
import { FreshnessBadge } from './FreshnessBadge'

${slice(1308, 1418)
  .replace('function busNodes', 'function busNodes')
  .replace('function Network', 'export function Network')}
`,
)

// ── Control ─────────────────────────────────────────────────────────
write(
  'src/components/Control.tsx',
  `import { useEffect, useRef, useState } from 'react'
import { api } from '../api'
import { findMsg, signalText } from '../lib/signals'
import { useAppStore } from '../store'
import { NumericDraft } from './NumericDraft'

${slice(1422, 2480)
  .replace('function DirectActuatorCards', 'function DirectActuatorCards')
  .replace('function Control', 'export function Control')}
`,
)

// ── Bench ───────────────────────────────────────────────────────────
write(
  'src/components/Bench.tsx',
  `import { useEffect, useState } from 'react'
import { api } from '../api'
import { activateTransportProfile } from '../lib/session'
import { PROFILE_LABELS, transportModeOf } from '../lib/signals'
import { useAppStore } from '../store'

${slice(2484, 2654).replace('function Bench', 'export function Bench')}
`,
)

// ── Diagnostics ─────────────────────────────────────────────────────
write(
  'src/components/Diagnostics.tsx',
  `import { useCallback, useEffect, useState } from 'react'
import { api } from '../api'
import { hexId } from '../lib/format'
import { useAppStore } from '../store'

${slice(2745, 3165).replace('function Diagnostics', 'export function Diagnostics')}
`,
)

// ── Logs ────────────────────────────────────────────────────────────
write(
  'src/components/Logs.tsx',
  `import { useCallback, useEffect, useState } from 'react'
import { api } from '../api'

${slice(3169, 3424).replace('function Logs', 'export function Logs')}
`,
)

// ── Settings ────────────────────────────────────────────────────────
write(
  'src/components/Settings.tsx',
  `import { useCallback, useEffect, useState } from 'react'
import { api } from '../api'
import { activateTransportProfile } from '../lib/session'
import { PROFILE_LABELS, shortHash, transportModeOf } from '../lib/signals'
import { useAppStore } from '../store'
import { IconCable, IconMonitor } from './icons'

${slice(3428, 4263)
  .replace(/^type RealSubProfile[^\n]+\n\nfunction shortHash[\s\S]*?\n}\n\n/, 'type RealSubProfile = \'bench_test\' | \'full_vehicle\'\n\n')
  .replace('function Settings', 'export function Settings')}
`,
)

// ── thin App ────────────────────────────────────────────────────────
write(
  'src/App.tsx',
  `import { useEffect, useRef } from 'react'
import { useAppStore } from './store'
import { useBackendStream } from './useStream'
import { VehiclePreview } from './VehiclePreview'
import { CanDictionary } from './CanDictionary'
import { Bench } from './components/Bench'
import { Control } from './components/Control'
import { Diagnostics } from './components/Diagnostics'
import { LiveCan } from './components/LiveCan'
import { Logs } from './components/Logs'
import { Network } from './components/Network'
import { Overview } from './components/Overview'
import { Settings } from './components/Settings'
import { Sidebar } from './components/Sidebar'
import { Topbar } from './components/Topbar'
import './App.css'

export default function App() {
  useBackendStream()
  const workspace = useAppStore((s) => s.workspace)
  const mainRef = useRef<HTMLElement>(null)
  useEffect(() => {
    mainRef.current?.scrollTo({ top: 0, left: 0 })
  }, [workspace])
  return (
    <div className="app" data-testid="app">
      <Topbar />
      <div className="body">
        <Sidebar />
        <main ref={mainRef}>
          {workspace === 'overview' && <Overview />}
          {workspace === 'network' && <Network />}
          {workspace === 'live' && <LiveCan />}
          {workspace === 'control' && <Control />}
          {workspace === 'preview' && <VehiclePreview />}
          {workspace === 'bench' && <Bench />}
          {workspace === 'dictionary' && <CanDictionary />}
          {workspace === 'diagnostics' && <Diagnostics />}
          {workspace === 'logs' && <Logs />}
          {workspace === 'settings' && <Settings />}
        </main>
      </div>
    </div>
  )
}
`,
)

console.log('done')
