<script lang="ts">
  import type { CanMessageDef } from "../lib/can-decoder";
  import { formatDecoded } from "../lib/can-decoder";
  import { latestById, recentFrameRate, stats, status, wsConnected } from "../stores/can";

  export let ids: CanMessageDef[] = [];

  $: speed = $latestById["0x120"]?.decoded.speed_mmps ?? "--";
  $: drive = $latestById["0x300"]?.decoded ?? {};
  $: safety = $latestById["0x011"]?.decoded ?? {};
  $: rtState = $latestById["0x210"]?.decoded ?? {};
  $: diag = $latestById["0x600"]?.decoded ?? {};
  $: obstacle = $latestById["0x400"]?.decoded.distance_label ?? $latestById["0x400"]?.decoded.distance_mm ?? "--";
  $: knownCount = ids.length;
</script>

<section class="dashboard-grid">
  <article class="metric">
    <span>Bus Load</span>
    <strong>{$stats.bus_load_pct.toFixed(1)}%</strong>
  </article>
  <article class="metric">
    <span>Frames/s</span>
    <strong>{Math.round($stats.frames_per_s || $recentFrameRate)}</strong>
  </article>
  <article class:danger={Boolean(safety.estop_active) || Boolean(diag.estop_active)} class="metric">
    <span>ESTOP</span>
    <strong>{safety.estop_active || diag.estop_active ? "ACTIVE" : "CLEAR"}</strong>
  </article>
  <article class="metric">
    <span>Mode</span>
    <strong>{rtState.mode_name ?? diag.mode_name ?? "--"}</strong>
  </article>
</section>

<section class="split-layout">
  <div class="panel">
    <div class="panel-title">
      <h2>Latest Values</h2>
      <span>{knownCount} IDs</span>
    </div>
    <div class="value-grid">
      <div><span>Speed</span><strong>{speed} mm/s</strong></div>
      <div><span>Yaw</span><strong>{drive.yaw_rate_mrad_s ?? "--"} mrad/s</strong></div>
      <div><span>Gear</span><strong>{drive.gear_name ?? "--"}</strong></div>
      <div><span>Brake</span><strong>{$latestById["0x301"]?.decoded.brake_pressure_kpa ?? "--"} kPa</strong></div>
      <div><span>Obstacle</span><strong>{obstacle}</strong></div>
      <div><span>Heap</span><strong>{diag.free_heap_kb ?? "--"} KB</strong></div>
    </div>
  </div>

  <div class="panel">
    <div class="panel-title">
      <h2>Links</h2>
      <span>{$wsConnected ? "WS live" : "WS idle"}</span>
    </div>
    <div class="link-stack">
      <div><span>Backend</span><strong>{$status.backend_online ? "online" : "offline"}</strong></div>
      <div><span>MQTT</span><strong>{$status.mqtt_connected ? "connected" : "disconnected"}</strong></div>
      <div><span>ESP32</span><strong>{$status.debug_esp32_online ? "online" : "offline"}</strong></div>
      <div><span>TEC / REC</span><strong>{$stats.tec} / {$stats.rec}</strong></div>
    </div>
  </div>
</section>

<section class="panel">
  <div class="panel-title">
    <h2>Last Frame Per ID</h2>
    <span>{Object.keys($latestById).length} active</span>
  </div>
  <div class="latest-list">
    {#each ids as item}
      <div class="latest-row">
        <span class="mono">{item.id}</span>
        <span>{item.name}</span>
        <strong>{formatDecoded($latestById[item.id]?.decoded ?? {})}</strong>
      </div>
    {/each}
  </div>
</section>
