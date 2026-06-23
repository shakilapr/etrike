<script lang="ts">
  import type { CanMessageDef } from "../lib/can-decoder";
  import { formatBytes, formatDecoded, frameTime } from "../lib/can-decoder";
  import type { StreamHandle } from "../lib/ws";
  import { frames } from "../stores/can";

  export let ids: CanMessageDef[] = [];
  export let stream: StreamHandle | null = null;

  let paused = false;
  let filterText = "";
  let expandedKey = "";
  let selectedIds = new Set<string>();

  $: visibleFrames = paused
    ? []
    : $frames.filter((frame) => {
        const matchesText =
          filterText.trim().length === 0 ||
          frame.id.toLowerCase().includes(filterText.toLowerCase()) ||
          frame.name.toLowerCase().includes(filterText.toLowerCase()) ||
          formatDecoded(frame.decoded).toLowerCase().includes(filterText.toLowerCase());
        const matchesId = selectedIds.size === 0 || selectedIds.has(frame.id);
        return matchesText && matchesId;
      });

  $: tableFrames = visibleFrames.slice(-300).reverse();

  function toggleId(id: string) {
    selectedIds = new Set(selectedIds);
    if (selectedIds.has(id)) {
      selectedIds.delete(id);
    } else {
      selectedIds.add(id);
    }
    stream?.setFilter([...selectedIds]);
  }

  function exportJson() {
    download("etrike-can-visible.json", JSON.stringify(tableFrames, null, 2), "application/json");
  }

  function exportCsv() {
    const rows = ["time,id,name,dlc,data,decoded"];
    for (const frame of tableFrames) {
      rows.push(
        [
          frameTime(frame),
          frame.id,
          frame.name,
          frame.dlc,
          formatBytes(frame.data),
          JSON.stringify(frame.decoded).replaceAll('"', '""')
        ]
          .map((cell) => `"${cell}"`)
          .join(",")
      );
    }
    download("etrike-can-visible.csv", rows.join("\n"), "text/csv");
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
      <input bind:value={filterText} placeholder="Filter frames" />
    </div>
    <div class="toolbar-actions">
      <button type="button" on:click={() => (paused = !paused)}>{paused ? "Resume" : "Pause"}</button>
      <button type="button" on:click={exportJson}>JSON</button>
      <button type="button" on:click={exportCsv}>CSV</button>
    </div>
  </div>

  <div class="filter-rail" aria-label="CAN ID filters">
    {#each ids as item}
      <button class:active={selectedIds.has(item.id)} type="button" on:click={() => toggleId(item.id)}>
        <span class="mono">{item.id}</span>
        <span>{item.name}</span>
      </button>
    {/each}
  </div>

  <div class="table-wrap">
    <table>
      <thead>
        <tr>
          <th>Timestamp</th>
          <th>ID / Name</th>
          <th>DLC</th>
          <th>Decoded</th>
        </tr>
      </thead>
      <tbody>
        {#each tableFrames as frame, index (`${frame.ts}-${frame.id}-${index}`)}
          <tr class:expanded={expandedKey === `${frame.ts}-${frame.id}-${index}`} on:click={() => (expandedKey = expandedKey === `${frame.ts}-${frame.id}-${index}` ? "" : `${frame.ts}-${frame.id}-${index}`)}>
            <td class="mono">{frameTime(frame)}</td>
            <td><span class="mono">{frame.id}</span> {frame.name}</td>
            <td>{frame.dlc}</td>
            <td>{formatDecoded(frame.decoded)}</td>
          </tr>
          {#if expandedKey === `${frame.ts}-${frame.id}-${index}`}
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
</section>
