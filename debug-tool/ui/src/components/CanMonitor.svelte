<script lang="ts">
  import MessageCard from "./MessageCard.svelte";
  import { CAN_INDEX, type CanMessageIndex } from "../lib/can-index";
  import type { Bus, CanField, CanFrame, CanMessageDef } from "../lib/can-decoder";
  import { formatBytes, formatDecoded, frameTime } from "../lib/can-decoder";
  import { frames, latestById } from "../stores/can";

  export let ids: CanMessageDef[] = [];

  interface Category {
    key: string;
    label: string;
    color: string;
    ids: string[];
  }

  const CATEGORIES: Category[] = [
    { key: "safety",    label: "Safety",      color: "var(--cat-safety)",    ids: ["0x001", "0x011"] },
    { key: "drive",     label: "Drive",       color: "var(--cat-drive)",     ids: ["0x120", "0x204", "0x206", "0x210", "0x300"] },
    { key: "steering",  label: "Steering",    color: "var(--cat-steering)",  ids: ["0x169", "0x201", "0x202", "0x203"] },
    { key: "brake",     label: "Brake",       color: "var(--cat-brake)",     ids: ["0x205", "0x301", "0x721", "0x731", "0x741", "0x7B9"] },
    { key: "diag",      label: "Diagnostics", color: "var(--cat-diag)",      ids: ["0x110", "0x220", "0x302", "0x310", "0x311", "0x400", "0x600"] },
    { key: "heartbeat", label: "Heartbeat",   color: "var(--cat-heartbeat)", ids: ["0x7FC", "0x7FD", "0x7FE"] },
    { key: "test",      label: "Test/System", color: "var(--cat-system)",    ids: ["0x012", "0x6FA", "0x6FB"] }
  ];

  type BusFilter = Bus | "all";
  let busFilter: BusFilter = "all";
  let paused = false;
  let pausedFrames: CanFrame[] = [];
  let pausedLatest: Record<string, CanFrame> = {};
  let filterText = "";
  let hideIdle = false;
  let collapsed = new Set<string>();
  let allExpanded = true;

  $: catalog = mergedCatalog(ids);
  $: liveLatest = paused ? pausedLatest : $latestById;
  $: sourceFrames = paused ? pausedFrames : $frames;
  $: filteredFrames = sourceFrames.filter((frame) => {
    const matchesBus = busFilter === "all" || frame.bus === busFilter;
    const text = filterText.trim().toLowerCase();
    const matchesText = text.length === 0 ||
      frame.id.toLowerCase().includes(text) ||
      frame.name.toLowerCase().includes(text) ||
      formatDecoded(frame.decoded).toLowerCase().includes(text);
    return matchesBus && matchesText;
  });
  $: filteredCatalog = catalog.filter((message) => {
    const frame = liveLatest[`${message.bus}:${message.id}`];
    const text = filterText.trim().toLowerCase();
    const matchesBus = busFilter === "all" || message.bus === busFilter;
    const matchesText = text.length === 0 ||
      message.id.toLowerCase().includes(text) ||
      message.name.toLowerCase().includes(text) ||
      message.sender.toLowerCase().includes(text) ||
      message.receivers.join(" ").toLowerCase().includes(text) ||
      message.signals.some((signal) => signal.name.toLowerCase().includes(text) || signal.comment.toLowerCase().includes(text)) ||
      (frame ? formatDecoded(frame.decoded).toLowerCase().includes(text) : false);
    const matchesActivity = !hideIdle || Boolean(frame);
    return matchesBus && matchesText && matchesActivity;
  });
  $: categoryGroups = CATEGORIES
    .map((category) => ({
      ...category,
      messages: filteredCatalog.filter((message) => category.ids.includes(message.id)),
      liveCount: filteredCatalog.filter((message) => category.ids.includes(message.id) && liveLatest[`${message.bus}:${message.id}`]).length
    }))
    .filter((category) => category.messages.length > 0);

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

  function legacyFor(message: CanMessageIndex): CanMessageDef | undefined {
    return ids.find((item) => item.bus === message.bus && item.id === message.id);
  }

  function togglePause() {
    if (!paused) {
      pausedFrames = $frames.slice();
      pausedLatest = { ...$latestById };
    }
    paused = !paused;
  }

  function toggleCat(key: string) {
    collapsed = new Set(collapsed);
    if (collapsed.has(key)) collapsed.delete(key); else collapsed.add(key);
    allExpanded = collapsed.size === 0;
  }

  function toggleAll() {
    if (allExpanded) {
      collapsed = new Set(categoryGroups.map((category) => category.key));
      allExpanded = false;
    } else {
      collapsed = new Set();
      allExpanded = true;
    }
  }

  function exportJson() {
    download(`etrike-can-${busFilter}.json`, JSON.stringify(filteredFrames, null, 2), "application/json");
  }

  function exportCsv() {
    const rows = ["time,bus,id,name,dlc,data,decoded"];
    for (const frame of filteredFrames) {
      rows.push(
        [frameTime(frame), frame.bus, frame.id, frame.name, frame.dlc, formatBytes(frame.data),
          JSON.stringify(frame.decoded).replaceAll('"', '""')].map((cell) => `"${cell}"`).join(",")
      );
    }
    download(`etrike-can-${busFilter}.csv`, rows.join("\n"), "text/csv");
  }

  function download(name: string, body: string, type: string) {
    const url = URL.createObjectURL(new Blob([body], { type }));
    const link = document.createElement("a");
    link.href = url;
    link.download = name;
    link.click();
    URL.revokeObjectURL(url);
  }
</script>

<section class="panel monitor-panel">
  <div class="toolbar">
    <div class="toolbar-main">
      <h2>CAN Monitor</h2>
      <div class="bus-tabs">
        <button class:active={busFilter === "all"}  type="button" on:click={() => (busFilter = "all")}>All</button>
        <button class:active={busFilter === "high"} type="button" on:click={() => (busFilter = "high")}>High</button>
        <button class:active={busFilter === "low"}  type="button" on:click={() => (busFilter = "low")}>Low</button>
      </div>
      <input bind:value={filterText} placeholder="Filter by ID, name, signal, ECU, or value" />
    </div>
    <div class="toolbar-actions">
      <label class="toggle-control">
        <input bind:checked={hideIdle} type="checkbox" />
        <span>Hide idle</span>
      </label>
      <button type="button" on:click={togglePause}>{paused ? "Resume" : "Pause"}</button>
      <button type="button" on:click={toggleAll}>{allExpanded ? "Collapse All" : "Expand All"}</button>
      <button type="button" on:click={exportJson}>JSON</button>
      <button type="button" on:click={exportCsv}>CSV</button>
    </div>
  </div>

  <div class="monitor-index">
    {#each categoryGroups as category}
      {@const isOpen = !collapsed.has(category.key)}
      <section class="index-category" style={`--cat-color:${category.color}`}>
        <button class="index-category-head" type="button" on:click={() => toggleCat(category.key)}>
          <span>{isOpen ? "v" : ">"}</span>
          <strong>{category.label}</strong>
          <em>{category.liveCount} live / {category.messages.length} shown</em>
        </button>
        {#if isOpen}
          <div class="message-grid">
            {#each category.messages as message (`${message.bus}:${message.id}:${message.name}`)}
              <MessageCard
                {message}
                frame={liveLatest[`${message.bus}:${message.id}`]}
                legacy={legacyFor(message)}
                categoryColor={category.color}
              />
            {/each}
          </div>
        {/if}
      </section>
    {:else}
      <div class="empty-state">No CAN messages match the current filters.</div>
    {/each}
  </div>
</section>

<style>
  .monitor-index {
    display: grid;
    gap: 12px;
    max-height: calc(100vh - 236px);
    overflow: auto;
    padding: 14px;
  }

  .index-category {
    border-left: 3px solid var(--cat-color);
    display: grid;
    gap: 10px;
    min-width: 0;
  }

  .index-category-head {
    align-items: center;
    background: var(--bg);
    border: 1px solid var(--panel-border);
    display: grid;
    gap: 10px;
    grid-template-columns: auto minmax(0, 1fr) auto;
    justify-items: start;
    min-height: 38px;
    padding: 8px 12px;
    text-align: left;
  }

  .index-category-head span {
    color: var(--cat-color);
    font-family: "Cascadia Mono", "SFMono-Regular", Consolas, monospace;
  }

  .index-category-head em {
    color: var(--muted);
    font-size: 0.76rem;
    font-style: normal;
    white-space: nowrap;
  }

  .message-grid {
    display: grid;
    gap: 12px;
    grid-template-columns: repeat(3, minmax(0, 1fr));
    padding-left: 10px;
  }

  .toggle-control {
    align-items: center;
    color: var(--muted);
    display: inline-flex;
    font-size: 0.78rem;
    font-weight: 700;
    gap: 6px;
    min-height: 36px;
    white-space: nowrap;
  }

  .toggle-control input {
    height: 16px;
    min-height: 16px;
    width: 16px;
  }

  .empty-state {
    color: var(--muted);
    padding: 18px 12px;
  }

  @media (max-width: 1200px) {
    .message-grid {
      grid-template-columns: repeat(2, minmax(0, 1fr));
    }
  }

  @media (max-width: 720px) {
    .monitor-index {
      max-height: none;
      padding: 10px;
    }

    .index-category-head,
    .message-grid {
      grid-template-columns: 1fr;
    }

    .message-grid {
      padding-left: 0;
    }
  }
</style>
