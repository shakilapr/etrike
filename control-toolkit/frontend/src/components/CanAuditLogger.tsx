import { useState, useMemo, useEffect, useRef } from "react"
import type { MessageState, Status } from "../store"
import { signalNum, frameRecent, findMsg, nodeStateLabel } from "../lib/signals"

export type CanAuditCategory = "refusal" | "state" | "report" | "fault"

export type CanAuditEntry = {
  id: string
  timestamp: string
  category: CanAuditCategory
  canIdHex: string
  msgName: string
  subsystem: "Steering" | "Braking" | "Speed" | "System" | "Safety"
  title: string
  description: string
  details?: Record<string, unknown>
}

export type CanAuditLoggerProps = {
  messages: MessageState[]
  status?: Status | null
  isDemo?: boolean
  testId?: string
}

const DEMO_ENTRIES: CanAuditEntry[] = [
  {
    id: "demo-1",
    timestamp: "12:04:12.410",
    category: "refusal",
    canIdHex: "0x115",
    msgName: "ESTOP_RESET_REPORT",
    subsystem: "Safety",
    title: "ESTOP Reset Refused by SYS",
    description: "SYS rejected 0x114 reset intent: active blocker asserted (SEB L3 fault active). Reset prohibited while cause is unrecovered.",
    details: { result: "REJECTED", active_blocker: "SEB_L3", latched_cause: "0x721 error_status=3" }
  },
  {
    id: "demo-2",
    timestamp: "12:04:14.820",
    category: "refusal",
    canIdHex: "0x201",
    msgName: "SES_STATUS",
    subsystem: "Steering",
    title: "Steering Autonomy Refused: Manual Override",
    description: "Driver applied 4.8 Nm manual counter-torque to handlebars. EPS controller suspended host guidance and yielded control to operator.",
    details: { torque_nm: 4.8, ses_mode: "MANUAL_OVERRIDE", host_target_deg: 14.0 }
  },
  {
    id: "demo-3",
    timestamp: "12:04:15.110",
    category: "refusal",
    canIdHex: "0x204",
    msgName: "RT_DRIVE_CMD",
    subsystem: "Speed",
    title: "Speed Demand Clamped by RT Safety Limiter",
    description: "Host requested 100 km/h (27778 mm/s), but RT clamped output to 97 km/h (26944 mm/s) based on active steer angle curvature limits.",
    details: { host_demand_mmps: 27778, rt_clamped_mmps: 26944, delta_kmh: -3.0 }
  },
  {
    id: "demo-4",
    timestamp: "12:04:16.050",
    category: "state",
    canIdHex: "0x012",
    msgName: "RT_STATUS",
    subsystem: "System",
    title: "RT Node State: STANDBY → ACTIVE",
    description: "RT supervisor confirmed heartbeats from SYS and MTR. Safety checks passed; closed-loop motion control loop engaged.",
    details: { prev_state: "STANDBY", new_state: "ACTIVE", safety_state: "0x01 (Nominal)" }
  },
  {
    id: "demo-5",
    timestamp: "12:04:16.420",
    category: "report",
    canIdHex: "0x721",
    msgName: "SEB_STATUS",
    subsystem: "Braking",
    title: "SEB Hydraulic Clamping Report",
    description: "Brake-by-wire transducer verified 1850 kPa hydraulic line pressure at 18.5 mm caliper travel. System calibrated and fully responsive.",
    details: { pressure_kpa: 1850, stroke_mm: 18.5, error_status: 0 }
  },
  {
    id: "demo-6",
    timestamp: "12:04:17.150",
    category: "report",
    canIdHex: "0x201",
    msgName: "SES_STATUS",
    subsystem: "Steering",
    title: "SES Steer-by-Wire Calibration Report",
    description: "EPS zero-point alignment confirmed valid. Measured actual angle: +12.4°, closed-loop error within ±0.2° tolerance.",
    details: { angle_deg: 12.4, aligned: true, fault_level: "L0_NORMAL" }
  },
  {
    id: "demo-7",
    timestamp: "12:04:17.650",
    category: "refusal",
    canIdHex: "0x169",
    msgName: "VCU_SES_REQ",
    subsystem: "Steering",
    title: "Steering Autonomy Refused: Control Enable Inactive",
    description: "SES Steer-by-Wire requires control_enable=1 (0x169) before engaging closed-loop tracking. Actuator is currently in passive mode.",
    details: { control_enable: 0, required: 1, action: "Set control_enable=1 in RT VCU_SES_REQ" }
  },
  {
    id: "demo-8",
    timestamp: "12:04:18.020",
    category: "report",
    canIdHex: "0x110",
    msgName: "SYS_MODE_CMD",
    subsystem: "System",
    title: "AUTO Mode Gate Matrix: 6/6 Controllers Synchronized",
    description: "SYS confirmed mode=AUTO (0x110), power=ON (0x113), zero-point aligned (0x201), SEB nominal (0x721). Autonomy pipeline fully cleared.",
    details: { sys_mode: "AUTO", contactor: "CLOSED", ses_aligned: true, seb_error: 0 }
  }
]

export function CanAuditLogger({
  messages,
  status: _status,
  isDemo = false,
  testId = "dashboard-can-logger",
}: CanAuditLoggerProps) {
  const [activeTab, setActiveTab] = useState<"all" | "refusal" | "state" | "report">("all")
  const [searchQuery, setSearchQuery] = useState("")
  const [isPaused, setIsPaused] = useState(false)
  const [expandedId, setExpandedId] = useState<string | null>(null)
  const [entries, setEntries] = useState<CanAuditEntry[]>([])

  const scrollRef = useRef<HTMLDivElement>(null)
  const lastObservedRef = useRef<{
    lastSpeedDelta?: number
    lastSesFault?: number
    lastSebFault?: number
    seenEventIds: Set<string>
  }>({ seenEventIds: new Set() })

  // Initialize with demo entries if no frames or in demo mode
  useEffect(() => {
    if (isDemo || messages.length === 0) {
      setEntries(DEMO_ENTRIES)
    }
  }, [isDemo, messages.length])

  // Real-time CAN frame and refusal detection
  useEffect(() => {
    if (isPaused || messages.length === 0) return

    const newItems: CanAuditEntry[] = []
    const now = new Date()
    const timeStr = now.toTimeString().split(" ")[0] + "." + String(now.getMilliseconds()).padStart(3, "0")
    const obs = lastObservedRef.current

    // 1. ESTOP Reset Refusal & ESTOP Transitions (0x115, 0x001)
    const resetRpt = findMsg(messages, "ESTOP_RESET_REPORT")
    if (resetRpt && frameRecent(resetRpt, 2000)) {
      const res = String(resetRpt.signals?.result?.enum_label ?? resetRpt.signals?.result?.engineering_value ?? "").toUpperCase()
      const eventKey = "reset-rpt-" + resetRpt.age_ms
      if (res && res !== "OK" && res !== "SUCCESS" && !obs.seenEventIds.has(eventKey)) {
        obs.seenEventIds.add(eventKey)
        newItems.push({
          id: "refusal-reset-" + Date.now(),
          timestamp: timeStr,
          category: "refusal",
          canIdHex: "0x115",
          msgName: "ESTOP_RESET_REPORT",
          subsystem: "Safety",
          title: "ESTOP Reset Refused by SYS",
          description: `SYS rejected reset attempt: ${res}. Hardware fault cause remains asserted on CAN bus.`,
          details: { result: res, bus: resetRpt.bus }
        })
      }
    }

    // 2. Steering Autonomy Refusal: Torque Override or Inhibit (0x201, 0x169)
    const sesStatus = findMsg(messages, "SES_STATUS")
    if (sesStatus && frameRecent(sesStatus, 1500)) {
      const torque = signalNum(sesStatus, "torque_nm")
      const sesErr = signalNum(sesStatus, "fault_level") ?? signalNum(sesStatus, "error_status")
      
      if (torque != null && Math.abs(torque) > 3.5 && obs.lastSesFault !== 999) {
        obs.lastSesFault = 999
        newItems.push({
          id: "refusal-steer-torque-" + Date.now(),
          timestamp: timeStr,
          category: "refusal",
          canIdHex: "0x201",
          msgName: "SES_STATUS",
          subsystem: "Steering",
          title: "Steering Autonomy Refused: Driver Override",
          description: `Driver applied ${Math.abs(torque).toFixed(1)} Nm manual counter-torque. Host tracking yielded to human operator.`,
          details: { torque_nm: torque, can_id: "0x201" }
        })
      }

      if (sesErr != null && sesErr > 0 && obs.lastSesFault !== sesErr) {
        obs.lastSesFault = sesErr
        newItems.push({
          id: "fault-ses-" + Date.now(),
          timestamp: timeStr,
          category: "fault",
          canIdHex: "0x201",
          msgName: "SES_STATUS",
          subsystem: "Steering",
          title: `SES Actuator Fault: Level L${sesErr}`,
          description: "Steer-by-wire controller reported internal sensor or motor drive warning.",
          details: { fault_level: sesErr }
        })
      }
    }

    // 3. Speed Demand Clamped or Discrepancy (0x300 vs 0x204)
    const hostDrive = findMsg(messages, "HOST_DRIVE_CMD", "high")
    const rtDrive = findMsg(messages, "RT_DRIVE_CMD", "low")
    if (hostDrive && rtDrive && frameRecent(hostDrive, 1500) && frameRecent(rtDrive, 1500)) {
      const vHost = signalNum(hostDrive, "speed_mmps")
      const vRt = signalNum(rtDrive, "motor_speed_mmps") ?? signalNum(rtDrive, "speed_mmps")
      if (vHost != null && vRt != null) {
        const delta = vHost - vRt
        if (delta > 400 && (!obs.lastSpeedDelta || Math.abs(delta - obs.lastSpeedDelta) > 300)) {
          obs.lastSpeedDelta = delta
          newItems.push({
            id: "refusal-spd-clamp-" + Date.now(),
            timestamp: timeStr,
            category: "refusal",
            canIdHex: "0x204",
            msgName: "RT_DRIVE_CMD",
            subsystem: "Speed",
            title: "Speed Demand Clamped by RT Safety Supervisor",
            description: `Host requested ${(vHost * 0.0036).toFixed(0)} km/h, but RT clamped speed to ${(vRt * 0.0036).toFixed(0)} km/h (Δ ${(delta * 0.0036).toFixed(0)} km/h).`,
            details: { host_mmps: vHost, rt_mmps: vRt, delta_mmps: delta }
          })
        }
      }
    }

    // 4. Brake Following Error / Mechanical Lever Override (0x721, 0x600)
    const sebStatus = findMsg(messages, "SEB_STATUS")
    if (sebStatus && frameRecent(sebStatus, 1500)) {
      const sebErr = signalNum(sebStatus, "error_status")
      if (sebErr != null && sebErr > 0 && obs.lastSebFault !== sebErr) {
        obs.lastSebFault = sebErr
        newItems.push({
          id: "fault-seb-" + Date.now(),
          timestamp: timeStr,
          category: sebErr >= 3 ? "refusal" : "fault",
          canIdHex: "0x721",
          msgName: "SEB_STATUS",
          subsystem: "Braking",
          title: `SEB Actuator ${sebErr >= 3 ? "L3 Fault (Reset Blocker)" : "Warning (L" + sebErr + ")"}`,
          description: `Smart Electronic Brake transducer reported status code ${sebErr}. ${sebErr >= 3 ? "Causes active ESTOP reset refusal." : "Degraded clamping performance."}`,
          details: { error_status: sebErr }
        })
      }
    }

    // 5. Node State Transitions
    for (const nodeName of ["SYS_STATUS", "RT_STATUS", "MTR_STATUS"]) {
      const nm = findMsg(messages, nodeName)
      if (nm && frameRecent(nm, 2000)) {
        const stateStr = nodeStateLabel(nm)
        const key = nodeName + "_state"
        const prev = (obs as Record<string, unknown>)[key] as string | undefined
        if (stateStr && prev && prev !== stateStr) {
          ;(obs as Record<string, unknown>)[key] = stateStr
          newItems.push({
            id: "state-" + nodeName + "-" + Date.now(),
            timestamp: timeStr,
            category: "state",
            canIdHex: nm.can_id ? "0x" + nm.can_id.toString(16).toUpperCase() : "0x011",
            msgName: nodeName,
            subsystem: "System",
            title: `Node State Transition: ${nodeName.replace("_STATUS", "")} ${prev} → ${stateStr}`,
            description: `Vehicle node ${nodeName} transitioned to ${stateStr} operating lifecycle state.`,
            details: { prev_state: prev, new_state: stateStr }
          })
        } else if (stateStr && !prev) {
          ;(obs as Record<string, unknown>)[key] = stateStr
        }
      }
    }

    // 6. Activation Signal Refusals & Gate Blockers (0x169, 0x600)
    const sysModeMsg = findMsg(messages, "SYS_MODE_CMD")
    const isAutoMode =
      sysModeMsg &&
      (signalNum(sysModeMsg, "mode") === 1 ||
        String(sysModeMsg.signals?.mode?.enum_label).toUpperCase() === "AUTO")

    if (isAutoMode) {
      // Check 0x169 control_enable
      const sesReq = findMsg(messages, "VCU_SES_REQ")
      if (sesReq && frameRecent(sesReq, 2000)) {
        const ctrlEn = signalNum(sesReq, "control_enable")
        if (ctrlEn === 0 && !obs.seenEventIds.has("refusal-ses-ctrlen")) {
          obs.seenEventIds.add("refusal-ses-ctrlen")
          newItems.push({
            id: "refusal-ses-ctrlen-" + Date.now(),
            timestamp: timeStr,
            category: "refusal",
            canIdHex: "0x169",
            msgName: "VCU_SES_REQ",
            subsystem: "Steering",
            title: "Steering Autonomy Refused: Control Enable Inactive (0x169)",
            description: "Vehicle is in AUTO mode, but RT has not asserted control_enable on 0x169. SES controller remains in passive manual assist.",
            details: { control_enable: 0, required: 1, mode: "AUTO" }
          })
        }
      }

      // Check 0x600 Handlebar Brake Lever Override
      const sysDiagMsg = findMsg(messages, "SYS_DIAG_RPT")
      if (sysDiagMsg && frameRecent(sysDiagMsg, 2000)) {
        const lever = signalNum(sysDiagMsg, "brake_engaged")
        if (lever === 1 && !obs.seenEventIds.has("refusal-lever-engaged")) {
          obs.seenEventIds.add("refusal-lever-engaged")
          newItems.push({
            id: "refusal-lever-" + Date.now(),
            timestamp: timeStr,
            category: "refusal",
            canIdHex: "0x600",
            msgName: "SYS_DIAG_RPT",
            subsystem: "Safety",
            title: "Autonomous Drive Refused: Handlebar Brake Lever Engaged (0x600)",
            description: "Physical brake lever sensor active (GPIO2). Hardware safety interlock immediately overrides autonomous traction.",
            details: { brake_engaged: 1, action: "Release brake lever to re-engage autonomy" }
          })
        }
      }
    }


    if (newItems.length > 0) {
      setEntries((prev) => [...newItems, ...prev].slice(0, 100))
    }
  }, [messages, isPaused])

  // Filtered entries based on tab & search query
  const filteredEntries = useMemo(() => {
    return entries.filter((e) => {
      if (activeTab === "refusal" && e.category !== "refusal") return false
      if (activeTab === "state" && e.category !== "state") return false
      if (activeTab === "report" && e.category !== "report" && e.category !== "fault") return false

      if (searchQuery.trim()) {
        const q = searchQuery.toLowerCase()
        return (
          e.title.toLowerCase().includes(q) ||
          e.description.toLowerCase().includes(q) ||
          e.canIdHex.toLowerCase().includes(q) ||
          e.msgName.toLowerCase().includes(q) ||
          e.subsystem.toLowerCase().includes(q)
        )
      }
      return true
    })
  }, [entries, activeTab, searchQuery])

  // Count of refusals in log
  const refusalCount = useMemo(() => {
    return entries.filter((e) => e.category === "refusal").length
  }, [entries])

  return (
    <div className="can-audit-card" data-testid={testId} aria-label="CAN Event and Refusal Audit Logger">
      {/* Logger Header */}
      <div className="can-audit-header">
        <div className="flex items-center gap-2">
          <span className="can-audit-title">CAN Audit & Refusal Log</span>
          <span className="can-audit-subtitle">Refusals · States · Reports</span>
        </div>

        {/* Refusal Alert Badge */}
        <div className="flex items-center gap-2">
          {refusalCount > 0 ? (
            <span className="can-audit-badge tone-refusal" title="Active CAN refusals or command rejections detected">
              ⚠ {refusalCount} Refusal{refusalCount > 1 ? "s" : ""}
            </span>
          ) : (
            <span className="can-audit-badge tone-nominal" title="No active refusals detected on CAN network">
              ✓ Stream Nominal
            </span>
          )}

          <button
            type="button"
            className="can-audit-btn-icon"
            onClick={() => setIsPaused((v) => !v)}
            title={isPaused ? "Resume real-time event stream" : "Pause event stream"}
          >
            {isPaused ? "▶" : "⏸"}
          </button>
          <button
            type="button"
            className="can-audit-btn-icon"
            onClick={() => setEntries([])}
            title="Clear audit log"
          >
            🗑
          </button>
        </div>
      </div>

      {/* Filter Tabs & Search Bar */}
      <div className="can-audit-controls">
        <div className="can-audit-tabs">
          <button
            type="button"
            className={"can-audit-tab " + (activeTab === "all" ? "is-active" : "")}
            onClick={() => setActiveTab("all")}
          >
            All ({entries.length})
          </button>
          <button
            type="button"
            className={"can-audit-tab " + (activeTab === "refusal" ? "is-active tab-refusal" : "")}
            onClick={() => setActiveTab("refusal")}
          >
            Refusals ({refusalCount})
          </button>
          <button
            type="button"
            className={"can-audit-tab " + (activeTab === "state" ? "is-active" : "")}
            onClick={() => setActiveTab("state")}
          >
            States ({entries.filter((e) => e.category === "state").length})
          </button>
          <button
            type="button"
            className={"can-audit-tab " + (activeTab === "report" ? "is-active" : "")}
            onClick={() => setActiveTab("report")}
          >
            Reports ({entries.filter((e) => e.category === "report" || e.category === "fault").length})
          </button>
        </div>

        <input
          type="text"
          className="can-audit-search"
          placeholder="Filter by CAN ID (0x...), keyword, or subsystem..."
          value={searchQuery}
          onChange={(e) => setSearchQuery(e.target.value)}
        />
      </div>

      {/* Scrollable Event Stream */}
      <div className="can-audit-stream" ref={scrollRef}>
        {filteredEntries.length === 0 ? (
          <div className="can-audit-empty">
            <span className="text-muted text-xs">No CAN events matching the current filter criteria</span>
          </div>
        ) : (
          filteredEntries.map((item) => {
            const isExpanded = expandedId === item.id
            return (
              <div
                key={item.id}
                className={"can-audit-entry category-" + item.category}
                onClick={() => setExpandedId(isExpanded ? null : item.id)}
              >
                {/* Entry Meta Row */}
                <div className="can-audit-entry-top">
                  <div className="flex items-center gap-1.5">
                    <span className={"can-audit-pill pill-" + item.category}>
                      {item.category.toUpperCase()}
                    </span>
                    <span className="can-id-tag" title={item.msgName}>
                      {item.canIdHex}
                    </span>
                    <span className="can-audit-subsystem">{item.subsystem}</span>
                  </div>
                  <span className="can-audit-time">{item.timestamp}</span>
                </div>

                {/* Entry Title & Body */}
                <div className="can-audit-entry-title">{item.title}</div>
                <div className="can-audit-entry-desc">{item.description}</div>

                {/* Expandable Details Payload */}
                {isExpanded && item.details && (
                  <div className="can-audit-entry-details">
                    <div className="can-audit-details-title">CAN Payload & Diagnostic Fields:</div>
                    <pre className="can-audit-json">{JSON.stringify(item.details, null, 2)}</pre>
                  </div>
                )}
              </div>
            )
          })
        )}
      </div>

      {/* Logger Footer Info */}
      <div className="can-audit-footer">
        <span className="text-muted text-xs">
          Showing {filteredEntries.length} of {entries.length} captured events · Live WebSocket Telemetry
        </span>
        {isPaused && <span className="can-audit-paused-indicator">STREAM PAUSED</span>}
      </div>
    </div>
  )
}
