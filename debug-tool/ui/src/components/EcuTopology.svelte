<script lang="ts">
  import { ecuPresence } from "../stores/telemetry";
  import { workMode } from "../stores/work-mode";

  interface EcuNode {
    id: string; name: string; bus: "high" | "low" | "both";
    x: number; y: number;
  }

  const NODES: EcuNode[] = [
    { id: "host", name: "HOST", bus: "high", x: 15, y: 25 },
    { id: "rt",   name: "RT",   bus: "both", x: 50, y: 25 },
    { id: "sys",  name: "SYS",  bus: "low",  x: 30, y: 70 },
    { id: "mtr",  name: "MTR",  bus: "low",  x: 50, y: 70 },
    { id: "ses",  name: "EPS-C",bus: "low",  x: 70, y: 70 },
    { id: "seb",  name: "SEB",  bus: "low",  x: 85, y: 70 },
  ];

  const BUS_LINES = [
    { x1: 15, y1: 25, x2: 50, y2: 25, bus: "high" },
    { x1: 30, y1: 70, x2: 85, y2: 70, bus: "low" },
  ];

  function nodeState(id: string): "real" | "emulated" | "missing" {
    const presence = ($ecuPresence as Record<string, boolean>)[id];
    if (presence) return "real";
    const simEcus = $workMode.simulatedEcus;
    if (simEcus.includes(id)) return "emulated";
    return "missing";
  }

  function stateColor(state: string): string {
    return state === "real" ? "#22c55e" : state === "emulated" ? "#3b82f6" : "#4b5563";
  }
</script>

<div class="topology" aria-label="ECU topology">
  <svg viewBox="0 0 100 95" class="topo-svg">
    <!-- Bus lines -->
    {#each BUS_LINES as line}
      <line x1={line.x1} y1={line.y1} x2={line.x2} y2={line.y2}
            stroke="#374151" stroke-width="0.8" stroke-dasharray="3,2" />
      <text x={(line.x1+line.x2)/2} y={line.y1-3} fill="#6b7280" font-size="2.5" text-anchor="middle">
        {line.bus.toUpperCase()} CAN
      </text>
    {/each}

    <!-- Vertical RT→low bus connection -->
    <line x1="50" y1="27" x2="50" y2="68" stroke="#374151" stroke-width="0.6" stroke-dasharray="2,2" />

    <!-- ECU nodes -->
    {#each NODES as node}
      {@const state = nodeState(node.id)}
      <circle cx={node.x} cy={node.y} r="5"
              fill="none" stroke={stateColor(state)} stroke-width="1.5"
              class:node-real={state === "real"}
              class:node-emulated={state === "emulated"} />
      <text x={node.x} y={node.y + 9} fill={stateColor(state)}
            font-size="3" text-anchor="middle" font-weight="bold">{node.name}</text>
      <!-- State badge -->
      <text x={node.x} y={node.y + 13} fill={stateColor(state)}
            font-size="2" text-anchor="middle" opacity="0.8">
        {state === "real" ? "● live" : state === "emulated" ? "● sim" : "○ off"}
      </text>
    {/each}
  </svg>
</div>

<style>
  .topology { padding: 0.5rem 1rem; background: var(--bg-card, #111827); border-radius: 6px; }
  .topo-svg { width: 100%; max-height: 200px; }
  .node-real { animation: pulse-green 2s infinite; }
  .node-emulated { animation: pulse-blue 2s infinite; }
  @keyframes pulse-green { 0%,100% { opacity: 1; } 50% { opacity: 0.4; } }
  @keyframes pulse-blue  { 0%,100% { opacity: 1; } 50% { opacity: 0.4; } }
</style>
