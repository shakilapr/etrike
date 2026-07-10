<script lang="ts">
  import type { CanField } from "@etrike/debug-shared";

  export let signal: CanField;
  export let value: unknown = undefined;
  export let stale = false;

  $: hasValue = value !== undefined && value !== null && value !== "";
  $: valueText = formatValue(value);
  $: unitText = signal.unit && signal.kind !== "enum" ? signal.unit : "";
  $: typeText = `${signal._type} ${signal._size}-bit`;
  $: scaleText = scaleFormula(signal._factor, signal._offset);
  $: layoutText = `B${signal._byte}.${signal._bit_offset}`;
  $: enumText = signal.options ? signal.options.map((opt) => `${opt.value}=${opt.label}`).join(", ") : "";
  $: title = [
    signal.key,
    `${layoutText}, ${typeText}`,
    `scale: ${scaleText}`,
    enumText ? `values: ${enumText}` : ""
  ].filter(Boolean).join("\n");

  function formatValue(input: unknown): string {
    if (input === undefined || input === null || input === "") return "--";
    if (typeof input === "boolean") return input ? "ON" : "OFF";
    if (typeof input === "number") return Number.isInteger(input) ? String(input) : input.toFixed(2);
    return String(input);
  }

  function scaleFormula(factor: number, offset: number): string {
    if (factor === 1 && offset === 0) return "raw";
    if (factor === 1) return `raw ${offset >= 0 ? "+" : "-"} ${Math.abs(offset)}`;
    if (offset === 0) return `raw x ${factor}`;
    return `raw x ${factor} ${offset >= 0 ? "+" : "-"} ${Math.abs(offset)}`;
  }
</script>

<div class="signal-box" class:is-stale={stale} class:is-empty={!hasValue} title={title}>
  <span>{signal.label}</span>
  <strong>{valueText}</strong>
  <small>{unitText || typeText}</small>
  <em>{layoutText}</em>
</div>

<style>
  .signal-box {
    background: var(--bg);
    border: 1px solid var(--panel-border);
    border-radius: 6px;
    display: grid;
    gap: 4px;
    min-height: 92px;
    min-width: 0;
    padding: 10px;
  }

  .signal-box span {
    color: var(--muted);
    font-size: 0.68rem;
    font-weight: 700;
    letter-spacing: 0.02em;
    overflow-wrap: anywhere;
    text-transform: uppercase;
  }

  .signal-box strong {
    color: var(--fg);
    font-family: "Cascadia Mono", "SFMono-Regular", Consolas, monospace;
    font-size: 1rem;
    line-height: 1.2;
    min-width: 0;
    overflow-wrap: anywhere;
  }

  .signal-box small,
  .signal-box em {
    color: var(--muted);
    font-size: 0.7rem;
    font-style: normal;
  }

  .signal-box.is-empty strong,
  .signal-box.is-stale strong {
    color: var(--muted);
  }
</style>
