<script lang="ts">
  import type { Bus, CanFrame, CanMessageDef } from "../lib/can-decoder";
  import { formatBytes, formatDecoded, frameTime } from "../lib/can-decoder";
  import { frames } from "../stores/can";

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
  let filterText = "";
  let expandedKey = "";
  let collapsed = new Set<string>();
  let allExpanded = true;

  $: sourceFrames = paused ? pausedFrames : $frames;
  $: catalog = ids.filter((message) => busFilter === "all" || message.bus === busFilter);
  $: categoryIds = CATEGORIES
    .map((category) => {
      const members = catalog.filter((message) => category.ids.includes(message.id));
      return { ...category, members, count: members.length };
    })
    .filter((category) => category.count > 0);

  $: filteredFrames = sourceFrames.filter((frame) => {
    const text = filterText.trim().toLowerCase();
    const matchesBus = busFilter === "all" || frame.bus === busFilter;
    const matchesText = text.length === 0 ||
      frame.id.toLowerCase().includes(text) ||
      frame.name.toLowerCase().includes(text) ||
      formatDecoded(frame.decoded).toLowerCase().includes(text);
    return matchesBus && matchesText;
  });

  function framesForCat(catId: string): CanFrame[] {
    const category = CATEGORIES.find((item) => item.key === catId);
    if (!category) return [];
    return filteredFrames.filter((frame) => category.ids.includes(frame.id)).slice(-10);
  }

  function memberIds(members: CanMessageDef[]): string {
    const labels = members.map((message) => `${message.bus.toUpperCase()} ${message.id}`);
    return Array.from(new Set(labels)).join(", ");
  }

  function togglePause() {
    if (!paused) pausedFrames = $frames.slice();
    paused = !paused;
  }

  function toggleCat(key: string) {
    collapsed = new Set(collapsed);
    if (collapsed.has(key)) collapsed.delete(key); else collapsed.add(key);
    allExpanded = collapsed.size === 0;
  }

  function toggleAll() {
    if (allExpanded) {
      collapsed = new Set(categoryIds.map((category) => category.key));
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
      <input bind:value={filterText} placeholder="Filter frames by ID, name, or value" />
    </div>
    <div class="toolbar-actions">
      <button type="button" on:click={togglePause}>{paused ? "Resume" : "Pause"}</button>
      <button type="button" on:click={toggleAll}>{allExpanded ? "Collapse All" : "Expand All"}</button>
      <button type="button" on:click={exportJson}>JSON</button>
      <button type="button" on:click={exportCsv}>CSV</button>
    </div>
  </div>

  <div class="monitor-cards">
    {#each categoryIds as category}
      {@const catFrames = framesForCat(category.key)}
      {@const isOpen = !collapsed.has(category.key)}
      <section class="cat-card" style={`--cat-color:${category.color}`}>
        <button class="cat-header" type="button" on:click={() => toggleCat(category.key)}>
          <span class="cat-arrow">{isOpen ? "v" : ">"}</span>
          <span class="cat-label">{category.label}</span>
          <span class="cat-badge">{catFrames.length} / {category.members.length} IDs</span>
        </button>
        {#if isOpen}
          <div class="cat-body">
            {#if catFrames.length === 0}
              <p class="cat-empty">
                <strong>No frames yet</strong>
                <span>Waiting for {memberIds(category.members)}</span>
              </p>
            {:else}
              <div class="cat-table-wrap">
                <table>
                  <thead>
                    <tr><th>Timestamp</th><th>Bus</th><th>ID</th><th>Decoded</th></tr>
                  </thead>
                  <tbody>
                    {#each catFrames.slice().reverse() as frame, index (`${category.key}-${frame.ts}-${frame.id}-${index}`)}
                      {@const rowKey = `${category.key}-${frame.ts}-${frame.id}-${index}`}
                      <tr
                        class:expanded={expandedKey === rowKey}
                        data-testid="frame-row"
                        on:click={() => (expandedKey = expandedKey === rowKey ? "" : rowKey)}
                      >
                        <td class="mono">{frameTime(frame)}</td>
                        <td><span class="bus-tag">{frame.bus}</span></td>
                        <td><span class="mono">{frame.id}</span> {frame.name}</td>
                        <td>{formatDecoded(frame.decoded)}</td>
                      </tr>
                      {#if expandedKey === rowKey}
                        <tr class="detail-row">
                          <td colspan="4">
                            <div><span>Raw</span><strong class="mono">{formatBytes(frame.data)}</strong></div>
                            <pre>{JSON.stringify(frame.decoded, null, 2)}</pre>
                          </td>
                        </tr>
                      {/if}
                    {/each}
                  </tbody>
                </table>
              </div>
            {/if}
          </div>
        {/if}
      </section>
    {:else}
      <div class="empty-state">No CAN IDs match the current bus filter.</div>
    {/each}
  </div>
</section>

<style>
  .cat-table-wrap {
    overflow-x: auto;
  }

  .empty-state {
    color: var(--muted);
    padding: 18px 12px;
  }
</style>
