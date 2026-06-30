<script lang="ts">
  import type { CanMessageIndex, CanSignalDef } from "../lib/can-index";

  export let message: CanMessageIndex;
  export let activeSignal = -1;

  const COLORS = [
    "#4ea1ff", "#e0556a", "#4caf82", "#e6b34a", "#b06bff", "#3dd6c8",
    "#ee5e5e", "#7bc96f", "#8f7cff", "#2fb6a7", "#e09f3e", "#6f91ff"
  ];

  function colorFor(index: number): string {
    return COLORS[index % COLORS.length];
  }

  function scaleFor(signal: CanSignalDef): string {
    if (signal.factor === 1 && signal.offset === 0) return "raw";
    if (signal.factor === 1) return `raw ${signal.offset >= 0 ? "+" : "-"} ${Math.abs(signal.offset)}`;
    if (signal.offset === 0) return `raw x ${signal.factor}`;
    return `raw x ${signal.factor} ${signal.offset >= 0 ? "+" : "-"} ${Math.abs(signal.offset)}`;
  }

  function valuesFor(signal: CanSignalDef): string {
    if (!signal.values || Object.keys(signal.values).length === 0) return "--";
    return Object.entries(signal.values).map(([key, value]) => `${key}=${value}`).join(", ");
  }

  function dash(value: string | number | null | undefined): string {
    if (value === undefined || value === null || value === "") return "--";
    return String(value);
  }
</script>

{#if message.signals.length === 0}
  <div class="signal-empty">No payload signals.</div>
{:else}
  <div class="signal-table-wrap">
    <table class="signal-table">
      <thead>
        <tr>
          <th>Signal</th>
          <th>Byte</th>
          <th>Bit</th>
          <th>Len</th>
          <th>Type</th>
          <th>Scale</th>
          <th>Unit</th>
          <th>Values</th>
          <th>Description</th>
        </tr>
      </thead>
      <tbody>
        {#each message.signals as signal, index}
          <tr
            class:highlight={activeSignal === index}
            on:mouseenter={() => (activeSignal = index)}
            on:mouseleave={() => (activeSignal = -1)}
            on:focusin={() => (activeSignal = index)}
            on:focusout={() => (activeSignal = -1)}
          >
            <td>
              <span class="sig-color" style={`--sig-color:${colorFor(index)}`}></span>
              <strong>{signal.name}</strong>
            </td>
            <td>{signal.byte}</td>
            <td>{signal.bit_offset}</td>
            <td>{signal.size}</td>
            <td>{signal.type}</td>
            <td>{scaleFor(signal)}</td>
            <td>{dash(signal.unit)}</td>
            <td>{valuesFor(signal)}</td>
            <td>{dash(signal.comment)}</td>
          </tr>
        {/each}
      </tbody>
    </table>
  </div>
{/if}

<style>
  .signal-table-wrap {
    overflow-x: auto;
  }

  .signal-table {
    border-collapse: collapse;
    font-size: 0.78rem;
    min-width: 880px;
    width: 100%;
  }

  .signal-table th,
  .signal-table td {
    border-bottom: 1px solid var(--panel-border);
    padding: 7px 9px;
    text-align: left;
    vertical-align: top;
  }

  .signal-table th {
    background: var(--bg);
    color: var(--muted);
    font-size: 0.68rem;
    font-weight: 800;
    letter-spacing: 0.03em;
    text-transform: uppercase;
  }

  .signal-table tr.highlight td,
  .signal-table tr:hover td {
    background: var(--hover-row);
  }

  .signal-table td {
    color: var(--fg);
  }

  .signal-table td:nth-child(8),
  .signal-table td:nth-child(9) {
    color: var(--muted);
    min-width: 160px;
  }

  .sig-color {
    background: var(--sig-color);
    border-radius: 2px;
    display: inline-block;
    height: 10px;
    margin-right: 7px;
    vertical-align: middle;
    width: 10px;
  }

  .signal-empty {
    color: var(--muted);
    font-size: 0.82rem;
  }
</style>
