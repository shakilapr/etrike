import { useMemo, useState } from 'react'
import { flexRender, getCoreRowModel, getFilteredRowModel, useReactTable, type ColumnDef } from '@tanstack/react-table'
import { useAppStore } from '../store'
import { BusBadge, StatusPill, Unknown } from './primitives'
import { FRESHNESS_CLASS, FRESHNESS_LABEL, formatAgeMs, ageMsFromLastSeen } from '../freshness'
import { useNow } from '../useNow'
import type { MessageState } from '../types'

function senderFor(catalog: { bus: string; id: number; sender: string }[], bus: string, canId: number): string | null {
  return catalog.find((i) => i.bus === bus && i.id === canId)?.sender ?? null
}

function signalSummary(msg: MessageState): string {
  const entries = Object.entries(msg.signals)
  if (entries.length === 0) return '—'
  return entries
    .slice(0, 4)
    .map(([k, v]) => `${k}=${v.enum_label ?? v.engineering_value ?? v.raw_value ?? '?'}`)
    .join('  ')
}

// Latest-by-message view (workplan §4.6 — the must-have per the exit gate).
// Uses @tanstack/react-table (named explicitly in the workplan) rather than a
// plain <table>: filtering/sorting on ~30-40 rows doesn't need it
// performance-wise, but it gives column filtering for free and matches the
// spec's stated tech choice. Chronological raw-frame view and the message
// detail drawer are deferred — no raw-frame API exists yet (only
// latest-value state), and the drawer needs the per-message bit-layout UI
// which is closer to Phase 6's CAN Dictionary workspace scope.
export function LiveCan() {
  const messages = useAppStore((s) => s.messages)
  const catalog = useAppStore((s) => s.catalog)
  const clockOffsetMs = useAppStore((s) => s.clockOffsetMs)
  const now = useNow()
  const [busFilter, setBusFilter] = useState<'' | 'high' | 'low'>('')
  const [freshnessFilter, setFreshnessFilter] = useState('')
  const [nameFilter, setNameFilter] = useState('')

  const rows = useMemo(() => {
    return messages.filter((m) => {
      if (busFilter && m.bus !== busFilter) return false
      if (freshnessFilter && m.freshness !== freshnessFilter) return false
      if (nameFilter && !(m.name ?? '').toLowerCase().includes(nameFilter.toLowerCase())) return false
      return true
    })
  }, [messages, busFilter, freshnessFilter, nameFilter])

  const columns = useMemo<ColumnDef<MessageState>[]>(
    () => [
      {
        header: 'Bus',
        accessorKey: 'bus',
        cell: (c) => <BusBadge bus={c.getValue<'high' | 'low'>()} />,
      },
      {
        header: 'ID',
        accessorKey: 'can_id',
        cell: (c) => <span className="font-mono">0x{c.getValue<number>().toString(16).toUpperCase()}</span>,
      },
      { header: 'Name', accessorKey: 'name', cell: (c) => c.getValue<string | null>() ?? <Unknown /> },
      {
        header: 'Sender',
        id: 'sender',
        cell: (c) => senderFor(catalog, c.row.original.bus, c.row.original.can_id) ?? <Unknown />,
      },
      {
        header: 'Rate (obs/exp)',
        id: 'rate',
        cell: (c) => {
          const m = c.row.original
          const obs = m.observed_rate_hz !== null ? `${m.observed_rate_hz.toFixed(1)}Hz` : '—'
          const exp = m.expected_rate_hz !== null ? `${m.expected_rate_hz.toFixed(1)}Hz` : '—'
          return (
            <span className="font-mono text-xs">
              {obs} / {exp}
            </span>
          )
        },
      },
      {
        header: 'Age',
        id: 'age',
        cell: (c) => <span className="font-mono text-xs">{formatAgeMs(ageMsFromLastSeen(c.row.original.last_seen_ns, now, clockOffsetMs))}</span>,
      },
      {
        header: 'Freshness',
        accessorKey: 'freshness',
        cell: (c) => {
          const f = c.getValue<MessageState['freshness']>()
          return <StatusPill label={FRESHNESS_LABEL[f]} colorClass={FRESHNESS_CLASS[f]} />
        },
      },
      {
        header: 'Decoded',
        id: 'decoded',
        cell: (c) => <span className="font-mono text-xs text-text-dim">{signalSummary(c.row.original)}</span>,
      },
    ],
    [catalog, now, clockOffsetMs],
  )

  const table = useReactTable({
    data: rows,
    columns,
    getCoreRowModel: getCoreRowModel(),
    getFilteredRowModel: getFilteredRowModel(),
    getRowId: (m) => `${m.bus}:${m.can_id}`,
  })

  return (
    <div className="flex flex-col gap-3 p-4">
      <div className="flex items-center gap-2 text-sm">
        <select value={busFilter} onChange={(e) => setBusFilter(e.target.value as typeof busFilter)} className="rounded border border-border bg-surface px-2 py-1">
          <option value="">All buses</option>
          <option value="high">High</option>
          <option value="low">Low</option>
        </select>
        <select value={freshnessFilter} onChange={(e) => setFreshnessFilter(e.target.value)} className="rounded border border-border bg-surface px-2 py-1">
          <option value="">All freshness</option>
          {Object.keys(FRESHNESS_LABEL).map((f) => (
            <option key={f} value={f}>
              {FRESHNESS_LABEL[f as keyof typeof FRESHNESS_LABEL]}
            </option>
          ))}
        </select>
        <input
          placeholder="Filter by name…"
          value={nameFilter}
          onChange={(e) => setNameFilter(e.target.value)}
          className="rounded border border-border bg-surface px-2 py-1"
        />
        <span className="ml-auto text-xs text-text-faint">{rows.length} / {messages.length} messages</span>
      </div>

      <table className="w-full border-collapse text-sm">
        <thead>
          {table.getHeaderGroups().map((hg) => (
            <tr key={hg.id} className="border-b border-border text-left text-xs uppercase text-text-faint">
              {hg.headers.map((h) => (
                <th key={h.id} className="px-2 py-1.5 font-medium">
                  {flexRender(h.column.columnDef.header, h.getContext())}
                </th>
              ))}
            </tr>
          ))}
        </thead>
        <tbody>
          {table.getRowModel().rows.map((row) => (
            <tr key={row.id} className="border-b border-border/60 hover:bg-surface-hover">
              {row.getVisibleCells().map((cell) => (
                <td key={cell.id} className="px-2 py-1.5">
                  {flexRender(cell.column.columnDef.cell, cell.getContext())}
                </td>
              ))}
            </tr>
          ))}
          {rows.length === 0 && (
            <tr>
              <td colSpan={columns.length} className="px-2 py-4 text-center text-text-faint">
                No messages observed yet
              </td>
            </tr>
          )}
        </tbody>
      </table>
    </div>
  )
}
