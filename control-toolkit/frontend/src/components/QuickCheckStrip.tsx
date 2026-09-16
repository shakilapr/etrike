import { getDiagnosticIndicators, type DiagnosticItem } from "../lib/signals"
import type { MessageState } from "../store"
import { StatusDot, type StatusDotTone } from "./ui/status-dot"


function toDotTone(status: DiagnosticItem["status"]): StatusDotTone {
  switch (status) {
    case "ok":
      return "success"
    case "warn":
      return "warning"
    case "danger":
      return "danger"
    case "info":
      return "tx"
    case "muted":
    default:
      return "muted"
  }
}

interface QuickCheckStripProps {
  messages: MessageState[]
}

export function QuickCheckStrip({ messages }: QuickCheckStripProps) {
  const diag = getDiagnosticIndicators(messages)

  const pods = [
    {
      id: "actuators",
      title: "Actuator Alignment & Health",
      items: diag.actuators,
    },
    {
      id: "interlocks",
      title: "Safety Interlocks & Guard",
      items: diag.interlocks,
    },
    {
      id: "can",
      title: "CAN Controller & FIFO Diagnostics",
      items: diag.canControllers,
    },
    {
      id: "relays",
      title: "12V Body Relays & Lighting",
      items: diag.relays,
    },
  ]

  return (
    <section
      className="quick-check-strip"
      data-testid="quick-check-strip"
      aria-label="Diagnostic indicators"
    >
      {pods.map((pod) => (
        <div key={pod.id} className="check-pod" data-testid={"check-pod-" + pod.id}>
          <div className="check-pod-header">
            <span className="check-pod-title">{pod.title}</span>
          </div>
          <div className="check-pod-items">
            {pod.items.map((item) => (
              <div
                key={item.id}
                className={"check-chip status-" + item.status}
                title={item.tooltip}
                data-testid={"check-chip-" + item.id}
              >
                <StatusDot tone={toDotTone(item.status)} />
                <span className="check-chip-label">{item.label}</span>
                <span className="check-chip-val">{item.valueText}</span>
              </div>
            ))}
          </div>
        </div>
      ))}
    </section>
  )
}
