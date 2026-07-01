<script lang="ts">
  import MessageCard from "./MessageCard.svelte";
  import { CAN_INDEX, type CanMessageIndex } from "../lib/can-index";
  import type { Bus, CanField, CanMessageDef } from "../lib/can-decoder";

  export let ids: CanMessageDef[] = [];

  type BusFilter = Bus | "all";
  let busFilter: BusFilter = "all";
  let filterText = "";

  $: catalog = mergedCatalog(ids);
  $: filteredCatalog = catalog.filter((message) => {
    const text = filterText.trim().toLowerCase();
    const matchesBus = busFilter === "all" || message.bus === busFilter;
    const matchesText = text.length === 0 ||
      message.id.toLowerCase().includes(text) ||
      message.name.toLowerCase().includes(text) ||
      message.sender.toLowerCase().includes(text) ||
      message.receivers.join(" ").toLowerCase().includes(text) ||
      message.comment.toLowerCase().includes(text) ||
      message.signals.some((signal) =>
        signal.name.toLowerCase().includes(text) ||
        signal.comment.toLowerCase().includes(text)
      );
    return matchesBus && matchesText;
  });
  $: visibleSignals = filteredCatalog.reduce((total, message) => total + message.signals.length, 0);
  $: fallbackMessages = filteredCatalog.filter((message) => message.protocol === "debug_api_fallback").length;
  $: generatedMessages = filteredCatalog.length - fallbackMessages;

  function mergedCatalog(apiIds: CanMessageDef[]): CanMessageIndex[] {
    const generated = new Map(CAN_INDEX.map((message) => [`${message.bus}:${message.id}`, message]));
    const fallback = apiIds
      .filter((item) => !generated.has(`${item.bus}:${item.id}`))
      .map(toIndexMessage);
    return [...CAN_INDEX, ...fallback].sort((a, b) =>
      a.bus.localeCompare(b.bus) || Number.parseInt(a.id.slice(2), 16) - Number.parseInt(b.id.slice(2), 16)
    );
  }

  function toIndexMessage(item: CanMessageDef): CanMessageIndex {
    return {
      bus: item.bus,
      id: item.id,
      name: item.name,
      dlc: item.dlc,
      sender: item.sender,
      receivers: [],
      cycle_ms: periodToMs(item.period),
      comment: "Fallback entry from the debug-tool API catalog. Add this message to shared/can/can_*.yaml for byte-level metadata.",
      byte_order: "motorola",
      protocol: "debug_api_fallback",
      signals: item.fields.map((field, index) => fieldToSignal(field, index))
    };
  }

  function fieldToSignal(field: CanField, index: number) {
    return {
      name: field.label.replace(/[^A-Za-z0-9]+/g, "_").replace(/^_|_$/g, "") || field.key,
      byte: Math.min(index, 7),
      bit_offset: 0,
      size: field.kind === "boolean" ? 1 : 8,
      type: "unsigned" as const,
      factor: 1,
      offset: 0,
      min: field.min ?? null,
      max: field.max ?? null,
      unit: field.unit ?? "",
      receivers: [],
      values: field.options ? Object.fromEntries(field.options.map((option) => [String(option.value), option.label])) : null,
      comment: field.key
    };
  }

  function periodToMs(period: string): number {
    const hz = period.match(/(\d+(?:\.\d+)?)\s*Hz/i);
    if (hz) return Math.round(1000 / Number(hz[1]));
    const ms = period.match(/(\d+)\s*ms/i);
    if (ms) return Number(ms[1]);
    return 0;
  }
</script>

<section class="panel monitor-panel">
  <div class="toolbar">
    <div class="toolbar-main">
      <div class="dictionary-title">
        <h2>CAN Dictionary</h2>
        <span>Signal reference</span>
      </div>
      <div class="bus-tabs">
        <button class:active={busFilter === "all"}  type="button" on:click={() => (busFilter = "all")}>All</button>
        <button class:active={busFilter === "high"} type="button" on:click={() => (busFilter = "high")}>High</button>
        <button class:active={busFilter === "low"}  type="button" on:click={() => (busFilter = "low")}>Low</button>
      </div>
      <input bind:value={filterText} placeholder="Search by CAN ID, name, signal, ECU, or comment" />
    </div>
    <div class="dictionary-count">
      <span>{filteredCatalog.length} messages</span>
      <span>{visibleSignals} signals</span>
    </div>
  </div>

  <div class="dictionary-summary">
    <span>{generatedMessages} generated from shared/can/can_*.yaml</span>
    {#if fallbackMessages > 0}
      <span class="warn">{fallbackMessages} API fallback</span>
    {/if}
    <a href="/docs/how-to-read-can-tables.md">How to read CAN tables</a>
  </div>

  <div class="dictionary-reference">
    {#each filteredCatalog as message (`${message.bus}:${message.id}:${message.name}`)}
      <MessageCard
        {message}
        categoryColor="var(--accent)"
        mode="dictionary"
      />
    {:else}
      <div class="empty-state">No CAN dictionary messages match the current filters.</div>
    {/each}
  </div>
</section>

<style>
  .dictionary-title {
    display: grid;
    gap: 2px;
    min-width: max-content;
  }

  .dictionary-title span {
    color: var(--muted);
    font-size: 0.72rem;
    font-weight: 700;
    text-transform: uppercase;
  }

  .dictionary-count,
  .dictionary-summary {
    align-items: center;
    color: var(--muted);
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
  }

  .dictionary-count span,
  .dictionary-summary span,
  .dictionary-summary a {
    background: var(--bg);
    border: 1px solid var(--panel-border);
    border-radius: 5px;
    color: var(--muted);
    font-size: 0.74rem;
    font-weight: 700;
    min-height: 28px;
    padding: 5px 9px;
    text-decoration: none;
  }

  .dictionary-summary {
    border-bottom: 1px solid var(--panel-border);
    padding: 8px 14px;
  }

  .dictionary-summary .warn {
    border-color: color-mix(in srgb, var(--warn) 55%, var(--panel-border));
    color: var(--warn);
  }

  .dictionary-reference {
    display: grid;
    gap: 12px;
    max-height: calc(100vh - 278px);
    overflow: auto;
    padding: 14px;
  }

  .empty-state {
    color: var(--muted);
    padding: 18px 12px;
  }

  @media (max-width: 720px) {
    .dictionary-reference {
      max-height: none;
      padding: 10px;
    }
  }
</style>
