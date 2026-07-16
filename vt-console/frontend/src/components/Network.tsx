import { useAppStore } from '../store'
import { buildTopology } from '../topology'
import { Card, StatusPill, Unknown } from './primitives'
import { FRESHNESS_CLASS, FRESHNESS_LABEL } from '../freshness'

const CAPABILITY_LABEL: Record<string, string> = {
  hw_timestamps: 'HW timestamps',
  tx_echo: 'TX echo',
  listen_only: 'Listen only',
  bus_off_reporting: 'Bus-off reporting',
  tec_rec_reporting: 'TEC/REC reporting',
}

// Node topology and bus health (workplan §4.5). Topology is derived
// client-side from the protocol catalog's declared sender field crossed with
// live message freshness — there's no backend topology aggregation endpoint
// yet (vtc/state/topology.py is an empty stub). Only the freshness-derived
// states are shown (Live/Late/Missing/Invalid/Unseen); Offline/Simulated/
// Fault need per-frame source provenance the API doesn't expose yet.
export function Network() {
  const catalog = useAppStore((s) => s.catalog)
  const messages = useAppStore((s) => s.messages)
  const status = useAppStore((s) => s.status)

  const nodes = buildTopology(catalog, messages)
  const channels = status?.adapter.channels ?? {}
  const capability = status?.adapter.capability

  return (
    <div className="flex flex-col gap-4 p-4">
      <Card title="Bus health">
        <div className="grid grid-cols-2 gap-3">
          {(['high', 'low'] as const).map((bus) => {
            const ch = channels[bus]
            return (
              <div key={bus} className="rounded border border-border p-3">
                <div className={bus === 'high' ? 'text-bus-high' : 'text-bus-low'}>{bus === 'high' ? 'High' : 'Low'} bus</div>
                {ch ? (
                  <div className="mt-1 flex flex-wrap gap-x-4 gap-y-1 text-xs text-text-dim">
                    <span>activity: {ch.activity}</span>
                    <span>rx: {ch.rx_count}</span>
                    <span>tx: {ch.tx_count}</span>
                    <span>overflow: {ch.rx_overflow}</span>
                  </div>
                ) : (
                  <Unknown />
                )}
              </div>
            )
          })}
        </div>
        {capability && (
          <div className="mt-3 flex flex-wrap gap-3 text-xs text-text-faint">
            {Object.entries(capability).map(([k, v]) => (
              <span key={k}>
                {CAPABILITY_LABEL[k] ?? k}: {v === null ? 'Unknown' : v ? 'yes' : 'no'}
              </span>
            ))}
          </div>
        )}
      </Card>

      <Card title="Nodes (from declared senders in the protocol catalog)">
        {nodes.length === 0 ? (
          <Unknown />
        ) : (
          <div className="flex flex-col gap-1.5">
            {nodes.map((n) => (
              <div key={n.node} className="flex items-center gap-3 rounded border border-border px-3 py-1.5 text-sm">
                <span className="w-24 font-medium">{n.node}</span>
                <StatusPill label={FRESHNESS_LABEL[n.worst]} colorClass={FRESHNESS_CLASS[n.worst]} />
                <span className="text-xs text-text-faint">
                  {n.live} live · {n.late} late · {n.missing} missing · {n.invalid} invalid · {n.unseen} unseen
                </span>
              </div>
            ))}
          </div>
        )}
      </Card>
    </div>
  )
}
