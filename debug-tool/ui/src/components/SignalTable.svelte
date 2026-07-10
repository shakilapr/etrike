<script lang="ts">
  import type { CanMessageDef, CanField } from "@etrike/debug-shared";

  export let message: CanMessageDef;
  export let activeSignal = -1;

  const COLORS = [
    "#4ea1ff", "#e0556a", "#4caf82", "#e6b34a", "#b06bff", "#3dd6c8",
    "#ee5e5e", "#7bc96f", "#8f7cff", "#2fb6a7", "#e09f3e", "#6f91ff"
  ];

  function colorFor(index: number): string {
    return COLORS[index % COLORS.length];
  }

  function scaleFor(signal: CanField): string {
    if (signal._factor === 1 && signal._offset === 0) return "raw";
    if (signal._factor === 1) return `raw ${signal._offset >= 0 ? "+" : "-"} ${Math.abs(signal._offset)}`;
    if (signal._offset === 0) return `raw x ${signal._factor}`;
    return `raw x ${signal._factor} ${signal._offset >= 0 ? "+" : "-"} ${Math.abs(signal._offset)}`;
  }

  function valuesFor(signal: CanField): string {
    if (!signal.options || signal.options.length === 0) return "--";
    return signal.options.map((opt) => `${opt.value}=${opt.label}`).join(", ");
  }

  function receiversFor(signal: CanField): string {
    if (message.receivers && message.receivers.length > 0) return message.receivers.join(", ");
    return "--";
  }

  function dash(value: string | number | null | undefined): string {
    if (value === undefined || value === null || value === "") return "--";
    return String(value);
  }
</script>

{#if message.fields.length === 0}
  <div class="signal-empty">No payload signals.</div>
{:else}
  <div class="signal-table-wrap">
    <table class="signal-table">
      <thead>
        <tr>
          <th>Signal</th>
          <th>Start</th>
          <th>Byte</th>
          <th>Bit</th>
          <th>Len</th>
          <th>Type</th>
          <th>Scale</th>
          <th>Min</th>
          <th>Max</th>
          <th>Unit</th>
          <th>Rx</th>
          <th>Values</th>
          <th>Description</th>
        </tr>
      </thead>
      <tbody>
        {#each message.fields as signal, index}
          <tr class:highlight={activeSignal === index}>
            <td data-label="Signal">
              <span class="sig-color" style={`--sig-color:${colorFor(index)}`}></span>
              <strong>{signal.label}</strong>
            </td>
            <td data-label="Start">B{signal._byte}.{signal._bit_offset}</td>
            <td data-label="Byte">{signal._byte}</td>
            <td data-label="Bit">{signal._bit_offset}</td>
            <td data-label="Len">{signal._size}</td>
            <td data-label="Type">{signal._type}</td>
            <td data-label="Scale">{scaleFor(signal)}</td>
            <td data-label="Min">{dash(signal.min)}</td>
            <td data-label="Max">{dash(signal.max)}</td>
            <td data-label="Unit">{dash(signal.unit)}</td>
            <td data-label="Rx">{receiversFor(signal)}</td>
            <td data-label="Values">{valuesFor(signal)}</td>
            <td data-label="Description">{dash(signal.key)}</td>
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
    min-width: 1120px;
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

  .signal-table tr.highlight td {
    background: color-mix(in srgb, var(--accent) 14%, var(--hover-row));
  }

  .signal-table td {
    color: var(--fg);
  }

  .signal-table td:nth-child(11),
  .signal-table td:nth-child(12),
  .signal-table td:nth-child(13) {
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

  @media (max-width: 720px) {
    .signal-table {
      min-width: 0;
    }

    .signal-table thead {
      display: none;
    }

    .signal-table,
    .signal-table tbody,
    .signal-table tr,
    .signal-table td {
      display: block;
      width: 100%;
    }

    .signal-table tr {
      border: 1px solid var(--panel-border);
      border-radius: 6px;
      margin-bottom: 8px;
      overflow: hidden;
    }

    .signal-table td {
      align-items: start;
      border-bottom: 1px solid var(--panel-border);
      display: grid;
      gap: 10px;
      grid-template-columns: 86px minmax(0, 1fr);
      padding: 7px 9px;
    }

    .signal-table td:last-child {
      border-bottom: 0;
    }

    .signal-table td::before {
      color: var(--muted);
      content: attr(data-label);
      font-size: 0.66rem;
      font-weight: 800;
      text-transform: uppercase;
    }
  }
</style>
