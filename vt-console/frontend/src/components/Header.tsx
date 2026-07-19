import { useAppStore } from '../store'
import { Badge, StatusPill, Unknown } from './primitives'

const STREAM_QUALITY_CLASS: Record<string, string> = {
  connecting: 'text-text-dim bg-surface-hover',
  live: 'text-live bg-live/10',
  delayed: 'text-late bg-late/10',
  lost: 'text-invalid bg-invalid/10',
}

const ADAPTER_HEALTH_CLASS: Record<string, string> = {
  absent: 'text-text-faint bg-surface-hover',
  opening: 'text-late bg-late/10',
  open: 'text-live bg-live/10',
  active: 'text-live bg-live/10',
  quiet: 'text-text-dim bg-surface-hover',
  degraded: 'text-late bg-late/10',
  recovering: 'text-recovering bg-recovering/10',
  closed: 'text-text-faint bg-surface-hover',
}

function ChannelActivityPill({ label, activity }: { label: string; activity?: string }) {
  const cls =
    activity === 'active' ? 'text-live bg-live/10' : activity === 'quiet' ? 'text-text-dim bg-surface-hover' : 'text-text-faint bg-surface-hover'
  return <StatusPill label={label} colorClass={cls} />
}

// Persistent status header (workplan §4.2). Vehicle power/mode
// requested-vs-confirmed and ESTOP are NOT rendered here yet: SessionState
// carries no such fields (Phase 3 deliberately left them out — they need
// Phase 5/6/7 HMI + diagnostics data sources). The Overview workspace shows
// what's derivable today from real signals instead.
export function Header() {
  const status = useAppStore((s) => s.status)
  const session = useAppStore((s) => s.session)
  const streamQuality = useAppStore((s) => s.streamQuality)
  const reconnectAttempts = useAppStore((s) => s.reconnectAttempts)
  const protocolMismatch = useAppStore((s) => s.protocolMismatch)

  const high = status?.adapter.channels['high']
  const low = status?.adapter.channels['low']

  return (
    <header className="flex h-13 shrink-0 items-center gap-4 border-b border-border bg-surface px-4 text-sm">
      <div className="font-semibold tracking-tight">VTC</div>

      <Badge className="bg-surface-raised text-text-dim">
        {session?.profile ?? 'no session'}
      </Badge>

      <div className="flex items-center gap-1.5">
        <span className="text-text-faint">Adapter</span>
        <StatusPill
          label={status?.adapter.health ?? 'absent'}
          colorClass={ADAPTER_HEALTH_CLASS[status?.adapter.health ?? 'absent']}
        />
      </div>

      <div className="flex items-center gap-1.5">
        <ChannelActivityPill label="High" activity={high?.activity} />
        <ChannelActivityPill label="Low" activity={low?.activity} />
      </div>

      <div className="flex items-center gap-1.5">
        <span className="text-text-faint">Bench TX</span>
        <StatusPill
          label={session?.bench_tx ?? 'disabled'}
          colorClass={session?.bench_tx === 'enabled' ? 'text-live bg-live/10' : 'text-text-dim bg-surface-hover'}
        />
      </div>

      <div className="ml-auto flex items-center gap-3">
        {protocolMismatch && (
          <StatusPill label="Protocol hash mismatch" colorClass="text-invalid bg-invalid/10" />
        )}
        {!protocolMismatch && status && (
          <span className="font-mono text-xs text-text-faint" title={status.wire_hash}>
            {status.wire_hash.slice(0, 8)}
          </span>
        )}
        {status ? null : <Unknown />}

        <StatusPill
          label={streamQuality === 'connecting' && reconnectAttempts > 0 ? `retry ${reconnectAttempts}` : streamQuality}
          colorClass={STREAM_QUALITY_CLASS[streamQuality]}
        />
      </div>
    </header>
  )
}
