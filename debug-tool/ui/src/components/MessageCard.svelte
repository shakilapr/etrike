<script lang="ts">
  import SignalBox from "./SignalBox.svelte";
  import type { CanMessageIndex, CanSignalDef } from "../lib/can-index";
  import type { CanFrame, CanMessageDef } from "../lib/can-decoder";
  import { formatBytes, formatDecoded, frameAge } from "../lib/can-decoder";

  export let message: CanMessageIndex;
  export let frame: CanFrame | undefined = undefined;
  export let legacy: CanMessageDef | undefined = undefined;
  export let categoryColor = "var(--muted)";

  let expanded = false;

  $: ageSeconds = frame ? Math.max(Date.now() / 1000 - (frame.ts_real ?? frame.ts), 0) : Number.POSITIVE_INFINITY;
  $: freshness = frame ? freshnessState(ageSeconds) : "idle";
  $: cycleLabel = message.cycle_ms > 0 ? `${message.cycle_ms} ms` : "event";
  $: routeLabel = `${message.sender} -> ${(message.receivers.length ? message.receivers : ["all"]).join(", ")}`;
  $: decodedText = frame ? formatDecoded(frame.decoded) : "No live frame";
  $: visibleSignals = message.signals.length > 0 ? message.signals : eventSignals();

  function freshnessState(age: number): "live" | "fresh" | "stale" | "old" | "idle" {
    if (age < 0.2) return "live";
    if (age < 1) return "fresh";
    if (age < 3) return "stale";
    return "old";
  }

  function eventSignals(): CanSignalDef[] {
    return [{
      name: "EVENT_FRAME",
      byte: 0,
      bit_offset: 0,
      size: 0,
      type: "unsigned",
      factor: 1,
      offset: 0,
      min: null,
      max: null,
      unit: "",
      receivers: message.receivers,
      values: null,
      comment: message.comment || "DLC=0 event frame"
    }];
  }

  function valueFor(signal: CanSignalDef, index: number): unknown {
    if (!frame) return undefined;
    const decoded = frame.decoded;
    if (signal.name in decoded) return decoded[signal.name];

    const legacyKey = legacy?.fields[index]?.key;
    if (legacyKey && legacyKey in decoded) return decoded[legacyKey];

    const target = normalizeName(signal.name);
    const match = Object.entries(decoded).find(([key]) => normalizeName(key) === target);
    return match?.[1];
  }

  function normalizeName(input: string): string {
    return input.replace(/[^a-zA-Z0-9]/g, "").toLowerCase();
  }
</script>

<article class="message-card" data-freshness={freshness} data-testid="frame-row" style={`--card-color:${categoryColor}`}>
  <button class="message-head" type="button" on:click={() => (expanded = !expanded)} aria-expanded={expanded}>
    <span class="message-id">{message.id}</span>
    <strong>{message.name}</strong>
    <span class="message-bus">{message.bus}</span>
  </button>

  <div class="message-meta">
    <span>{routeLabel}</span>
    <span>DLC {message.dlc}</span>
    <span>{cycleLabel}</span>
    <span>{message.byte_order}</span>
  </div>

  {#if message.comment}
    <p class="message-comment">{message.comment}</p>
  {/if}

  <div class="signal-grid">
    {#each visibleSignals as signal, index}
      <SignalBox {signal} value={valueFor(signal, index)} stale={freshness === "stale" || freshness === "old"} />
    {/each}
  </div>

  <div class="freshness-row">
    <span>{frame ? frameAge(frame) : "idle"}</span>
    <span>{decodedText}</span>
  </div>

  {#if expanded}
    <div class="message-detail">
      <div><span>Raw</span><strong class="mono">{frame ? formatBytes(frame.data) : "--"}</strong></div>
      <pre>{frame ? JSON.stringify(frame.decoded, null, 2) : JSON.stringify(message, null, 2)}</pre>
    </div>
  {/if}
</article>

<style>
  .message-card {
    background: var(--panel);
    border: 1px solid var(--panel-border);
    border-left: 4px solid var(--card-color);
    border-radius: 8px;
    display: grid;
    gap: 10px;
    min-width: 0;
    padding: 12px;
  }

  .message-card[data-freshness="live"],
  .message-card[data-freshness="fresh"] {
    border-top-color: color-mix(in srgb, var(--ok) 60%, var(--panel-border));
  }

  .message-card[data-freshness="stale"] {
    border-top-color: var(--warn);
  }

  .message-card[data-freshness="old"] {
    border-top-color: var(--err);
  }

  .message-head {
    align-items: center;
    background: transparent;
    border: 0;
    display: grid;
    gap: 8px;
    grid-template-columns: auto minmax(0, 1fr) auto;
    min-height: 28px;
    padding: 0;
    text-align: left;
  }

  .message-head:hover {
    color: var(--accent);
  }

  .message-id,
  .message-bus {
    color: var(--accent);
    font-family: "Cascadia Mono", "SFMono-Regular", Consolas, monospace;
    font-size: 0.78rem;
    font-weight: 700;
    text-transform: uppercase;
  }

  .message-head strong {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .message-meta {
    color: var(--muted);
    display: flex;
    flex-wrap: wrap;
    gap: 6px 12px;
    font-size: 0.74rem;
  }

  .message-comment {
    color: var(--muted);
    font-size: 0.78rem;
    line-height: 1.4;
    margin: 0;
  }

  .signal-grid {
    display: grid;
    gap: 8px;
    grid-template-columns: repeat(auto-fit, minmax(118px, 1fr));
  }

  .freshness-row {
    align-items: center;
    border-top: 1px solid var(--panel-border);
    color: var(--muted);
    display: grid;
    gap: 10px;
    grid-template-columns: 72px minmax(0, 1fr);
    min-width: 0;
    padding-top: 9px;
  }

  .freshness-row span {
    font-size: 0.74rem;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .message-detail {
    border-top: 1px solid var(--panel-border);
    display: grid;
    gap: 8px;
    padding-top: 10px;
  }

  .message-detail span {
    color: var(--muted);
    font-size: 0.72rem;
    font-weight: 700;
    text-transform: uppercase;
  }

  .message-detail pre {
    margin: 0;
  }

  @media (max-width: 560px) {
    .freshness-row {
      grid-template-columns: 1fr;
    }
  }
</style>
