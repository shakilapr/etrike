<script lang="ts">
  import BitGrid from "./BitGrid.svelte";
  import SignalBox from "./SignalBox.svelte";
  import SignalTable from "./SignalTable.svelte";
  import type { CanField, CanFrame, CanMessageDef } from "../lib/can-decoder";
  import { formatBytes, formatDecoded, frameAge } from "../lib/can-decoder";

  export let message: CanMessageDef;
  export let frame: CanFrame | undefined = undefined;
  export let legacy: CanMessageDef | undefined = undefined;
  export let categoryColor = "var(--muted)";
  export let mode: "monitor" | "dictionary" = "monitor";

  let expanded = false;
  let activeSignal = -1;

  $: ageSeconds = frame ? Math.max(Date.now() / 1000 - timestampSeconds(frame.ts_real ?? frame.ts), 0) : Number.POSITIVE_INFINITY;
  $: freshness = frame ? freshnessState(ageSeconds) : "idle";
  $: cycleLabel = message.period;
  $: decodedText = frame ? formatDecoded(frame.decoded) : "No live frame";
  $: visibleSignals = message.fields.length > 0 ? message.fields : eventSignals();
  $: showLiveSummary = mode === "monitor";
  $: showDictionary = mode === "dictionary";
  $: isFallback = false;
  $: receiverLabel = (message.receivers?.length ? message.receivers : ["all"]).join(", ");
  $: receivers = message.receivers?.length ? message.receivers : ["all"];
  $: signalCountLabel = message.fields.length === 1 ? "1 signal" : `${message.fields.length} signals`;
  $: canExpandRaw = mode === "monitor" && Boolean(frame);
  $: expandLabel = canExpandRaw ? (expanded ? "Hide raw frame detail" : "Show raw frame detail") : "No live raw frame to expand";

  function freshnessState(age: number): "live" | "fresh" | "stale" | "old" | "idle" {
    if (age < 0.2) return "live";
    if (age < 1) return "fresh";
    if (age < 3) return "stale";
    return "old";
  }

  function timestampSeconds(stamp: number | undefined): number {
    if (!stamp) return 0;
    return stamp > 1_000_000_000_000 ? stamp / 1000 : stamp;
  }

  function eventSignals(): CanField[] {
    return [{
      key: "EVENT_FRAME",
      label: "EVENT_FRAME",
      kind: "number",
      _byte: 0,
      _bit_offset: 0,
      _size: 0,
      _type: "unsigned",
      _factor: 1,
      _offset: 0,
      min: undefined,
      max: undefined,
      unit: "",
      options: undefined,
    }];
  }

  function valueFor(signal: CanField, index: number): unknown {
    if (!frame) return undefined;
    const decoded = frame.decoded;
    if (signal.key in decoded) return decoded[signal.key];

    const legacyKey = legacy?.fields[index]?.key;
    if (legacyKey && legacyKey in decoded) return decoded[legacyKey];

    const target = normalizeName(signal.key);
    const match = Object.entries(decoded).find(([key]) => normalizeName(key) === target);
    return match?.[1];
  }

  function normalizeName(input: string): string {
    return input.replace(/[^a-zA-Z0-9]/g, "").toLowerCase();
  }

  function toggleRawDetail() {
    if (canExpandRaw) expanded = !expanded;
  }
</script>

<article
  class="message-card"
  class:is-dictionary={mode === "dictionary"}
  class:is-fallback={isFallback}
  data-freshness={freshness}
  data-testid="frame-row"
  style={`--card-color:${categoryColor}`}
>
  <button class="message-head" class:no-raw={!canExpandRaw} type="button" on:click={toggleRawDetail} aria-expanded={expanded} title={expandLabel}>
    <span class="message-arrow" aria-hidden="true">{canExpandRaw ? (expanded ? "v" : ">") : ""}</span>
    <span class="message-id">{message.id}</span>
    <strong>{message.name}</strong>
    <span class="message-bus">{message.bus}</span>
  </button>

  <div class="message-meta">
    <span class="badge sender" title="Sender ECU">TX {message.sender}</span>
    <span class="badge receiver" title="Receiver ECU(s)">RX {receiverLabel}</span>
    <span class="badge" title="CAN payload length">DLC {message.dlc}</span>
    <span class="badge" title="Transmit period">{cycleLabel}</span>
    <span class="badge" title="Signal byte order">{message.byteOrder}</span>
    <span class="badge" title="Signal count">{signalCountLabel}</span>
    <span class="badge source" class:fallback={isFallback} title={isFallback ? "Fallback entry from the debug-tool API catalog" : "Generated from shared/can/can_*.yaml"}>
      {isFallback ? "API fallback" : "YAML"}
    </span>
  </div>

  {#if isFallback}
    <div class="metadata-warning" role="note">
      No YAML byte map for this message. Signal positions are debug API fallback positions.
    </div>
  {/if}

  {#if mode === "dictionary"}
    <div class="route-map" aria-label={`${message.name} sender and receivers`}>
      <span class="route-node tx">TX {message.sender}</span>
      <span class="route-arrow" aria-hidden="true">-&gt;</span>
      <span class="route-receivers">
        {#each receivers as receiver}
          <span class="route-node rx">RX {receiver}</span>
        {/each}
      </span>
    </div>
  {/if}

  {#if message.comment && mode === "monitor"}
    <p class="message-comment">{message.comment}</p>
  {/if}

  {#if showLiveSummary}
    <div class="signal-grid">
      {#each visibleSignals as signal, index}
        <SignalBox {signal} value={valueFor(signal, index)} stale={freshness === "stale" || freshness === "old"} />
      {/each}
    </div>

    <div class="freshness-row">
      <span>{frame ? frameAge(frame) : "idle"}</span>
      <span>{decodedText}</span>
    </div>
  {/if}

  {#if showDictionary}
    <section class="dictionary-detail" data-testid="dictionary-detail">
      {#if message.comment}
        <p class="message-comment">{message.comment}</p>
      {/if}
      <BitGrid {message} bind:activeSignal />
      <SignalTable {message} bind:activeSignal />
    </section>
  {/if}

  {#if expanded && frame}
    <div class="message-detail">
      <div><span>Raw</span><strong class="mono">{frame ? formatBytes(frame.data) : "--"}</strong></div>
      <pre>{JSON.stringify(frame.decoded, null, 2)}</pre>
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

  .message-card.is-fallback {
    border-right-color: color-mix(in srgb, var(--warn) 45%, var(--panel-border));
  }

  .message-card.is-dictionary {
    border-left-width: 1px;
  }

  .message-card.is-dictionary .message-head {
    background: color-mix(in srgb, var(--bg) 72%, var(--panel));
    border-bottom: 1px solid var(--panel-border);
    margin: -12px -12px 0;
    padding: 10px 12px;
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
    grid-template-columns: auto auto minmax(0, 1fr) auto;
    min-height: 28px;
    padding: 0;
    text-align: left;
  }

  .message-head:hover {
    color: var(--accent);
  }

  .message-head.no-raw {
    cursor: default;
  }

  .message-head.no-raw:hover {
    color: inherit;
  }

  .message-arrow {
    color: var(--card-color);
    font-family: "Cascadia Mono", "SFMono-Regular", Consolas, monospace;
    font-size: 0.78rem;
    font-weight: 800;
    width: 12px;
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
    display: flex;
    flex-wrap: wrap;
    gap: 6px;
    min-width: 0;
  }

  .badge {
    background: var(--bg);
    border: 1px solid var(--panel-border);
    border-radius: 4px;
    color: var(--muted);
    display: inline-flex;
    font-size: 0.7rem;
    font-weight: 800;
    line-height: 1.2;
    min-height: 24px;
    max-width: 100%;
    padding: 4px 7px;
    text-transform: uppercase;
  }

  .badge.sender {
    border-color: color-mix(in srgb, var(--cat-drive) 45%, var(--panel-border));
    color: var(--cat-drive);
  }

  .badge.receiver {
    border-color: color-mix(in srgb, var(--ok) 45%, var(--panel-border));
    color: var(--ok);
  }

  .badge.source {
    border-color: color-mix(in srgb, var(--accent) 45%, var(--panel-border));
    color: var(--accent);
  }

  .badge.fallback {
    border-color: color-mix(in srgb, var(--warn) 55%, var(--panel-border));
    color: var(--warn);
  }

  .metadata-warning {
    background: color-mix(in srgb, var(--warn) 12%, var(--bg));
    border: 1px solid color-mix(in srgb, var(--warn) 45%, var(--panel-border));
    border-radius: 6px;
    color: var(--warn);
    font-size: 0.76rem;
    font-weight: 700;
    padding: 8px 10px;
  }

  .route-map {
    align-items: center;
    background: var(--bg);
    border: 1px solid var(--panel-border);
    border-radius: 6px;
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    padding: 8px 10px;
  }

  .route-node {
    border: 1px solid var(--panel-border);
    border-radius: 4px;
    font-size: 0.72rem;
    font-weight: 800;
    padding: 4px 8px;
    text-transform: uppercase;
  }

  .route-node.tx {
    border-color: color-mix(in srgb, var(--cat-drive) 55%, var(--panel-border));
    color: var(--cat-drive);
  }

  .route-node.rx {
    border-color: color-mix(in srgb, var(--ok) 50%, var(--panel-border));
    color: var(--ok);
  }

  .route-arrow {
    color: var(--accent);
    font-family: "Cascadia Mono", "SFMono-Regular", Consolas, monospace;
    font-weight: 800;
  }

  .route-receivers {
    display: flex;
    flex-wrap: wrap;
    gap: 6px;
  }

  .message-comment {
    color: var(--muted);
    font-size: 0.78rem;
    line-height: 1.4;
    margin: 0;
  }

  .dictionary-detail {
    border-top: 1px solid var(--panel-border);
    display: grid;
    gap: 12px;
    min-width: 0;
    padding-top: 10px;
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
