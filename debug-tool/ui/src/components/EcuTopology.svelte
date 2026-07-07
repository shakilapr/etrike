<script lang="ts">
  import { ecuPresence } from "../stores/telemetry";
  import { workMode } from "../stores/work-mode";

  interface EcuNode {
    id: string; name: string; bus: "high" | "low" | "both";
    x: number; y: number;
  }

  const NODES: EcuNode[] = [
    { id: "host", name: "HOST",  bus: "high", x: 8,  y: 20 },
    { id: "rt",   name: "RT",    bus: "both", x: 34, y: 20 },
    { id: "sys",  name: "SYS",   bus: "low",  x: 16, y: 68 },
    { id: "mtr",  name: "MTR",   bus: "low",  x: 36, y: 68 },
    { id: "ses",  name: "SES",   bus: "low",  x: 56, y: 68 },
    { id: "seb",  name: "SEB",   bus: "low",  x: 76, y: 68 },
  ];

  function nodeState(id: string): "real" | "emulated" | "missing" {
    const presence = ($ecuPresence as Record<string, boolean>)[id];
    if (presence) return "real";
    return $workMode.simulatedEcus.includes(id) ? "emulated" : "missing";
  }

  function stateColor(state: string): string {
    return state === "real" ? "#22c55e" : state === "emulated" ? "#3b82f6" : "#4b5563";
  }
</script>

<div class="topology" aria-label="ECU topology">
  <svg viewBox="0 0 100 95" class="topo-svg">
    <!-- High bus: HOST — RT -->
    <line x1="12" y1="20" x2="30" y2="20" stroke="#60a5fa" stroke-width="1.2" />
    <text x="21" y="15" fill="#60a5fa" font-size="3" text-anchor="middle" font-weight="bold">HIGH CAN</text>
    <line x1="38" y1="20" x2="84" y2="20" stroke="#60a5fa" stroke-width="0.6" stroke-dasharray="4,3" opacity="0.4" />

    <!-- Low bus -->
    <line x1="12" y1="68" x2="80" y2="68" stroke="#f59e0b" stroke-width="1.2" />
    <text x="46" y="63" fill="#f59e0b" font-size="3" text-anchor="middle" font-weight="bold">LOW CAN</text>

    <!-- RT → low bus connection -->
    <line x1="34" y1="24" x2="34" y2="64" stroke="#6b7280" stroke-width="0.8" stroke-dasharray="3,3" />

    <!-- ECU nodes -->
    {#each NODES as node}
      {@const state = nodeState(node.id)}
      <circle cx={node.x} cy={node.y} r="4.5"
              fill={state === "missing" ? "none" : stateColor(state)}
              fill-opacity={state === "emulated" ? "0.15" : "1"}
              stroke={stateColor(state)} stroke-width="1.8"
              class:node-live={state !== "missing"} />
      <text x={node.x} y={node.y + 9} fill={stateColor(state)}
            font-size="3" text-anchor="middle" font-weight="bold"
            font-family="monospace">{node.name}</text>
      <text x={node.x} y={node.y + 13.5} fill={stateColor(state)}
            font-size="2.2" text-anchor="middle" opacity="0.7"
            font-family="monospace">
        {state === "real" ? "LIVE" : state === "emulated" ? "SIM" : "OFF"}
      </text>
    {/each}
  </svg>
</div>

<style>
  .topology { padding: 0.5rem 1rem; background: var(--bg-card, #0f172a); border-radius: 6px; margin-bottom: 8px; }
  .topo-svg { width: 100%; max-height: 180px; }
  .node-live { animation: topo-pulse 2.5s infinite; }
  @keyframes topo-pulse { 0%,100% { opacity: 1; } 50% { opacity: 0.35; } }
</style>
