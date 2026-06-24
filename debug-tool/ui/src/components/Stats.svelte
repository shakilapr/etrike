<script lang="ts">
  import type { Bus, CanMessageDef } from "../lib/can-decoder";
  import { stats } from "../stores/can";

  export let ids: CanMessageDef[] = [];

  $: high = $stats.buses.high;
  $: low = $stats.buses.low;

  function maxCount(byId: Record<string, number>): number {
    return Math.max(1, ...Object.values(byId));
  }

  $: highMax = maxCount(high.by_id);
  $: lowMax = maxCount(low.by_id);

  function barsFor(bus: Bus, max: number): Array<CanMessageDef & { count: number; pct: number }> {
    const byId = bus === "high" ? high.by_id : low.by_id;
    return ids
      .filter((item) => item.bus === bus)
      .map((item) => ({
        ...item,
        count: byId[item.id] ?? 0,
        pct: ((byId[item.id] ?? 0) / max) * 100
      }));
  }

  $: highBars = barsFor("high", highMax);
  $: lowBars = barsFor("low", lowMax);
</script>

<section class="stats-layout">
  <div class="panel gauge-panel">
    <div class="panel-title">
      <h2>High Bus Load</h2>
      <span>{high.fps.toFixed(0)} fps</span>
    </div>
    <div class="gauge" style={`--value:${Math.min(high.load_pct, 100)}`}>
      <strong>{high.load_pct.toFixed(1)}%</strong>
    </div>
  </div>

  <div class="panel gauge-panel">
    <div class="panel-title">
      <h2>Low Bus Load</h2>
      <span>{low.fps.toFixed(0)} fps</span>
    </div>
    <div class="gauge" style={`--value:${Math.min(low.load_pct, 100)}`}>
      <strong>{low.load_pct.toFixed(1)}%</strong>
    </div>
  </div>

  <div class="panel counters-full">
    <div class="panel-title">
      <h2>Controller Counters</h2>
      <span>TEC / REC</span>
    </div>
    <div class="counter-grid">
      <div class:danger={high.tec > 0}><span>High TEC</span><strong>{high.tec}</strong></div>
      <div class:danger={high.rec > 0}><span>High REC</span><strong>{high.rec}</strong></div>
      <div class:danger={low.tec > 0}><span>Low TEC</span><strong>{low.tec}</strong></div>
      <div class:danger={low.rec > 0}><span>Low REC</span><strong>{low.rec}</strong></div>
      <div><span>Uptime</span><strong>{Math.round($stats.uptime_s)} s</strong></div>
      <div><span>Total (H+L)</span><strong>{high.total + low.total}</strong></div>
    </div>
  </div>
</section>

<section class="panel">
  <div class="panel-title">
    <h2>High Bus — Frames By ID</h2>
    <span>{Object.keys(high.by_id).length} active</span>
  </div>
  <div class="bar-list">
    {#each highBars as row}
      <div class="bar-row">
        <span class="mono">{row.id}</span>
        <span>{row.name}</span>
        <div class="bar-track"><div class="bar-fill" style={`width:${row.pct}%`}></div></div>
        <strong>{row.count}</strong>
      </div>
    {/each}
  </div>
</section>

<section class="panel">
  <div class="panel-title">
    <h2>Low Bus — Frames By ID</h2>
    <span>{Object.keys(low.by_id).length} active</span>
  </div>
  <div class="bar-list">
    {#each lowBars as row}
      <div class="bar-row">
        <span class="mono">{row.id}</span>
        <span>{row.name}</span>
        <div class="bar-track"><div class="bar-fill" style={`width:${row.pct}%`}></div></div>
        <strong>{row.count}</strong>
      </div>
    {/each}
  </div>
</section>
