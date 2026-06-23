<script lang="ts">
  import type { CanMessageDef } from "../lib/can-decoder";
  import { stats } from "../stores/can";

  export let ids: CanMessageDef[] = [];

  $: maxCount = Math.max(1, ...Object.values($stats.by_id));
  $: knownBars = ids.map((item) => ({
    ...item,
    count: $stats.by_id[item.id] ?? 0,
    pct: (($stats.by_id[item.id] ?? 0) / maxCount) * 100
  }));
</script>

<section class="stats-layout">
  <div class="panel gauge-panel">
    <div class="panel-title">
      <h2>Bus Load</h2>
      <span>{$stats.frames_per_s.toFixed(0)} fps</span>
    </div>
    <div class="gauge" style={`--value:${Math.min($stats.bus_load_pct, 100)}`}>
      <strong>{$stats.bus_load_pct.toFixed(1)}%</strong>
    </div>
  </div>

  <div class="panel">
    <div class="panel-title">
      <h2>Controller Counters</h2>
      <span>TEC / REC</span>
    </div>
    <div class="counter-grid">
      <div class:danger={$stats.tec > 0}><span>TEC</span><strong>{$stats.tec}</strong></div>
      <div class:danger={$stats.rec > 0}><span>REC</span><strong>{$stats.rec}</strong></div>
      <div><span>Uptime</span><strong>{Math.round($stats.uptime_s)} s</strong></div>
      <div><span>Total</span><strong>{$stats.total_frames}</strong></div>
    </div>
  </div>
</section>

<section class="panel">
  <div class="panel-title">
    <h2>Frames By ID</h2>
    <span>{Object.keys($stats.by_id).length} active</span>
  </div>
  <div class="bar-list">
    {#each knownBars as row}
      <div class="bar-row">
        <span class="mono">{row.id}</span>
        <span>{row.name}</span>
        <div class="bar-track"><div class="bar-fill" style={`width:${row.pct}%`}></div></div>
        <strong>{row.count}</strong>
      </div>
    {/each}
  </div>
</section>
