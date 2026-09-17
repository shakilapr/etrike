import { useState } from 'react'
import type { ActivationAuditReport } from '../lib/activationGates'
import { StatusPill } from './primitives'

export type ControllerActivationMatrixProps = {
  report: ActivationAuditReport
  compact?: boolean
  onSelectGate?: (gateId: string) => void
  testId?: string
}

export function ControllerActivationMatrix({
  report,
  compact = true,
  onSelectGate,
  testId = 'activation-matrix',
}: ControllerActivationMatrixProps) {
  const [expandedGateId, setExpandedGateId] = useState<string | null>(null)

  const toggleGate = (id: string) => {
    setExpandedGateId((prev) => (prev === id ? null : id))
    onSelectGate?.(id)
  }

  return (
    <div className="activation-gates-panel" data-testid={testId} aria-label="Controller Activation Gates">
      {/* ── Header Row: Title, Readiness Badge & Quick Summary ── */}
      <div className="activation-gates-header">
        <div className="flex items-center gap-2.5">
          <span className="activation-gates-title">Controller Activation & AUTO Mode Gates</span>
          <span
            className={`activation-gates-badge tone-${report.allCleared ? 'ready' : 'inhibited'}`}
            data-testid="activation-gates-status-badge"
            title={report.blockers.length > 0 ? report.blockers.join(' | ') : 'All prerequisite controller signals confirmed'}
          >
            {report.allCleared
              ? `✓ AUTO Ready · ${report.clearedGates}/${report.totalGates} Gates Cleared`
              : `⚠ AUTO Inhibited · ${report.totalGates - report.clearedGates} Blocker(s)`}
          </span>
        </div>

        <div className="flex items-center gap-2 text-xs text-muted">
          <span>Required CAN IDs:</span>
          <div className="flex items-center gap-1">
            <span className="can-id-tag" title="SYS Authority: 0x110 (Mode), 0x113 (Power), 0x011 (Safety)">0x110</span>
            <span className="can-id-tag" title="SES Steer Enable: 0x169 (Control Enable), 0x201 (Zero Alignment)">0x169</span>
            <span className="can-id-tag" title="MTR Propulsion: 0x204 (RT Drive Cmd), 0x206 (Echo)">0x204</span>
            <span className="can-id-tag" title="SEB Smart Brake: 0x7B9 (Auto Brake Req), 0x721 (Caliper Health)">0x7B9</span>
            <span className="can-id-tag" title="RT Supervisor: 0x012 (State), 0x7FD (Heartbeat)">0x012</span>
            <span className="can-id-tag" title="Host Navigation: 0x300 (Trajectory 20Hz), 0x303 (Steer)">0x300</span>
          </div>
        </div>
      </div>

      {/* ── Compact Cards Grid (6 Controllers) ── */}
      {compact && (
        <div className="activation-gates-grid" data-testid="activation-gates-grid">
          {report.gates.map((gate) => {
            const isSelected = expandedGateId === gate.id
            const isBlocked = gate.status === 'inhibited'
            return (
              <div
                key={gate.id}
                className={`activation-gate-card tone-${gate.status} ${isSelected ? 'is-selected' : ''}`}
                onClick={() => toggleGate(gate.id)}
                title={`Click to toggle signal details for ${gate.controller}`}
                data-testid={`gate-card-${gate.id}`}
              >
                {/* Gate Card Top: Controller Name & Status */}
                <div className="activation-gate-card-top">
                  <span className="activation-gate-controller">{gate.controller}</span>
                  <span
                    className={`activation-gate-dot dot-${gate.status}`}
                    title={gate.statusLabel}
                  />
                </div>

                {/* Gate CAN IDs */}
                <div className="activation-gate-can-row">
                  {gate.primaryCanIds.map((cid) => (
                    <span
                      key={cid}
                      className="can-id-tag"
                      title={gate.signals.find((s) => s.canId === cid)?.msgName ?? cid}
                    >
                      {cid}
                    </span>
                  ))}
                  <span className="activation-gate-subsystem">{gate.subsystem}</span>
                </div>

                {/* Gate Status & Blocker */}
                <div className="activation-gate-status-text">
                  {isBlocked ? (
                    <span className="text-danger font-semibold text-[11px] truncate" title={gate.blocker ?? 'Inhibited'}>
                      ⚠ {gate.blocker ?? 'Inhibited'}
                    </span>
                  ) : (
                    <span className="text-success font-medium text-[11px] truncate">
                      ✓ {gate.statusLabel}
                    </span>
                  )}
                </div>

                {/* Expanded Micro-Details if clicked */}
                {isSelected && (
                  <div className="activation-gate-expanded-signals">
                    {gate.signals.map((sig) => (
                      <div key={sig.id} className="activation-gate-micro-signal">
                        <div className="flex items-center justify-between gap-1">
                          <span
                            className="mono font-semibold text-[10px] text-text cursor-help border-b border-dotted border-border"
                            title={`${sig.msgName}.${sig.signalKey} — ${sig.description}`}
                          >
                            {sig.canId} {sig.condition}
                          </span>
                          <span
                            className={`text-[10px] font-bold ${
                              sig.isMet ? 'text-success' : 'text-danger'
                            }`}
                          >
                            {sig.isMet ? '✓ Pass' : '✗ Fail'}
                          </span>
                        </div>
                        <div className="text-[9.5px] text-muted truncate" title={sig.impactIfMissing}>
                          {sig.description}
                        </div>
                      </div>
                    ))}
                  </div>
                )}
              </div>
            )
          })}
        </div>
      )}

      {/* ── Full Detailed Table (For Tabbed Inspection View) ── */}
      {!compact && (
        <div className="overflow-x-auto" data-testid="activation-gates-full-table">
          <table className="dashboard-table">
            <thead>
              <tr>
                <th>Controller / Node</th>
                <th>Subsystem</th>
                <th>CAN ID & Bus</th>
                <th>Prerequisite Signal (Hover for Key)</th>
                <th>Required Condition</th>
                <th>Observed Value</th>
                <th>Gate Status</th>
                <th>Failure Impact If Missing</th>
              </tr>
            </thead>
            <tbody>
              {report.gates.flatMap((gate) =>
                gate.signals.map((sig, idx) => (
                  <tr key={sig.id} className={sig.isMet ? '' : 'row-blocked'}>
                    {idx === 0 ? (
                      <td
                        rowSpan={gate.signals.length}
                        className="font-bold text-xs align-top border-r border-border bg-surface-2"
                      >
                        <div>{gate.controller}</div>
                        <div className="mt-1">
                          <StatusPill
                            label={gate.statusLabel}
                            tone={gate.status === 'ready' ? 'ok' : 'danger'}
                          />
                        </div>
                      </td>
                    ) : null}
                    <td className="text-xs text-muted">{gate.subsystem}</td>
                    <td className="mono text-xs font-bold text-text">
                      <span
                        className="can-id-tag"
                        title={`${sig.bus.toUpperCase()} CAN ID ${sig.canId}`}
                      >
                        {sig.canId}
                      </span>
                      <span className="text-[10px] text-muted ml-1 uppercase">
                        {sig.bus}
                      </span>
                    </td>
                    <td>
                      <span
                        className="cursor-help font-medium text-xs text-text border-b border-dashed border-border"
                        title={`${sig.msgName}.${sig.signalKey} — ${sig.description}`}
                      >
                        {sig.msgName}
                      </span>
                      <span className="block text-[10px] text-muted font-normal mt-0.5">
                        {sig.description}
                      </span>
                    </td>
                    <td className="mono text-xs text-muted font-medium">
                      {sig.condition}
                    </td>
                    <td className="mono font-bold text-xs">
                      {sig.liveValue != null ? String(sig.liveValue) : '—'}
                    </td>
                    <td>
                      <span
                        className={`text-xs font-bold ${
                          sig.isMet ? 'text-success' : 'text-danger'
                        }`}
                      >
                        {sig.isMet ? '✓ Cleared' : '✗ Blocked'}
                      </span>
                    </td>
                    <td className="text-xs text-muted max-w-[280px]">
                      {sig.impactIfMissing}
                    </td>
                  </tr>
                )),
              )}
            </tbody>
          </table>
        </div>
      )}
    </div>
  )
}
