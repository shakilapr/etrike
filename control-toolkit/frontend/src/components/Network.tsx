import { hexId } from '../lib/format'
import { useAppStore, type TopologyNode } from '../store'
import { LivenessBadge } from './primitives'
import { WorkspaceShell } from './WorkspaceShell'

function busNodes(nodes: TopologyNode[], bus: string) {
  return nodes.filter((n) => n.bus === bus)
}

export function Network() {
  const topology = useAppStore((s) => s.topology)
  const status = useAppStore((s) => s.status)
  const quality = useAppStore((s) => s.streamQuality)
  const high = status?.adapter?.channels?.high
  const low = status?.adapter?.channels?.low
  const highNodes = busNodes(topology, 'high')
  const lowNodes = busNodes(topology, 'low')

  return (
    <WorkspaceShell
      testId="workspace-network"
      title="Network"
      description="ECU topology and bus health · High and Low never collapsed into one lamp"
    >

      <section className="bus-health" data-testid="bus-health">
        <div className="health-card">
          <h3>High bus</h3>
          <dl className="kv">
            <dt>Activity</dt>
            <dd>{high?.activity ?? '—'}</dd>
            <dt>RX frames</dt>
            <dd className="mono">{high?.rx_count ?? 0}</dd>
            <dt>TX frames</dt>
            <dd className="mono">{high?.tx_count ?? 0}</dd>
            <dt>Overflow</dt>
            <dd className="mono">{high?.rx_overflow ?? 0}</dd>
          </dl>
        </div>
        <div className="health-card">
          <h3>Low bus</h3>
          <dl className="kv">
            <dt>Activity</dt>
            <dd>{low?.activity ?? '—'}</dd>
            <dt>RX frames</dt>
            <dd className="mono">{low?.rx_count ?? 0}</dd>
            <dt>TX frames</dt>
            <dd className="mono">{low?.tx_count ?? 0}</dd>
            <dt>Overflow</dt>
            <dd className="mono">{low?.rx_overflow ?? 0}</dd>
          </dl>
        </div>
        <div className="health-card">
          <h3>Connection layers</h3>
          <dl className="kv">
            <dt>USB adapter</dt>
            <dd>{status?.adapter?.health ?? '—'}</dd>
            <dt>Backend stream</dt>
            <dd>{quality}</dd>
            <dt>Destination</dt>
            <dd>{status?.session?.destination ?? '—'}</dd>
            <dt>Adapter epoch</dt>
            <dd className="mono">{status?.adapter?.adapter_epoch ?? '—'}</dd>
          </dl>
        </div>
      </section>

      <section className="topology" data-testid="topology-map">
        <h2>Topology map</h2>
        <div className="bus-row">
          <div className="bus-label">High</div>
          <div className="bus-line high">
            {highNodes.map((n) => (
              <div
                key={`${n.bus}-${n.node}`}
                className={`topo-node liveness-${n.liveness}`}
                data-testid={`node-${n.node}`}
                title={`${n.node} ${hexId(n.can_id)} · ${n.liveness}`}
              >
                <div className="topo-name">{n.node}</div>
                <div className="mono muted">{hexId(n.can_id)}</div>
                <LivenessBadge value={n.liveness} />
              </div>
            ))}
            {highNodes.length === 0 && (
              <div className="topology-empty muted">No High-bus nodes observed.</div>
            )}
          </div>
        </div>
        <div className="bus-bridge muted">RT gateway bridges High ↔ Low domains</div>
        <div className="bus-row">
          <div className="bus-label">Low</div>
          <div className="bus-line low">
            {lowNodes.map((n) => (
              <div
                key={`${n.bus}-${n.node}`}
                className={`topo-node liveness-${n.liveness}`}
                data-testid={`node-${n.node}`}
                title={`${n.node} ${hexId(n.can_id)} · ${n.liveness}`}
              >
                <div className="topo-name">{n.node}</div>
                <div className="mono muted">{hexId(n.can_id)}</div>
                <LivenessBadge value={n.liveness} />
              </div>
            ))}
            {lowNodes.length === 0 && (
              <div className="topology-empty muted">No Low-bus nodes observed.</div>
            )}
          </div>
        </div>
      </section>
    </WorkspaceShell>
  )
}
