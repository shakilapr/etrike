<script lang="ts">
  import MessageCard from "./MessageCard.svelte";
  import { CAN_MESSAGES, type Bus, type CanField, type CanMessageDef } from "@etrike/debug-shared";

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
      message.fields.some((signal) =>
        signal.label.toLowerCase().includes(text) ||
        (signal.key || "").toLowerCase().includes(text)
      );
    return matchesBus && matchesText;
  });
  $: visibleSignals = filteredCatalog.reduce((total, message) => total + message.fields.length, 0);
  $: fallbackMessages = 0;
  $: generatedMessages = filteredCatalog.length;

  function mergedCatalog(apiIds: CanMessageDef[]): CanMessageDef[] {
    const generated = new Map(CAN_MESSAGES.map((message) => [`${message.bus}:${message.id}`, message]));
    const fallback = apiIds
      .filter((item) => !generated.has(`${item.bus}:${item.id}`));
    return [...CAN_MESSAGES, ...fallback].sort((a, b) =>
      a.bus.localeCompare(b.bus) || Number.parseInt(a.id.slice(2), 16) - Number.parseInt(b.id.slice(2), 16)
    );
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
      <input class="dictionary-search" bind:value={filterText} placeholder="Search by CAN ID, name, signal, ECU, or comment" />
    </div>
    <div class="dictionary-count">
      <span>{filteredCatalog.length} messages</span>
      <span>{visibleSignals} signals</span>
    </div>
  </div>

  <div class="dictionary-summary">
      <span>{generatedMessages} canonical protocol messages</span>
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

  .dictionary-search {
    max-width: 440px;
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
