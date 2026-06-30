<script lang="ts">
  import type { Bus, CanFrame, CanMessageDef } from "../lib/can-decoder";
  import { formatBytes, formatDecoded, frameAge, frameTime } from "../lib/can-decoder";
  import { latestById, stats, status, wsConnected } from "../stores/can";

  export let ids: CanMessageDef[] = [];

  type SignalState = "ok" | "warn" | "danger" | "idle";

  interface PairRow {
    label: string;
    command?: CanFrame;
    feedback?: CanFrame;
    commandLabel: string;
    feedbackLabel: string;
    state: SignalState;
  }

  $: high = $stats.buses.high;
  $: low = $stats.buses.low;
  $: adapter = $status.bridge?.adapter ?? "Adapter";
  $: transport = $status.bridge?.transport ?? "serial";
  $: linkOpen = Boolean($status.bridge?.link_open ?? $status.serial?.port_open);
  $: adapterOnline = Boolean($status.adapter_connected ?? $status.esp32_connected);

  $: safety = $latestById["high:0x011"]?.decoded ?? $latestById["low:0x011"]?.decoded ?? {};
  $: diag = $latestById["high:0x600"]?.decoded ?? $latestById["low:0x600"]?.decoded ?? {};
  $: rtState = $latestById["high:0x210"]?.decoded ?? {};
  $: drive = $latestById["high:0x300"]?.decoded ?? {};
  $: rtDrive = $latestById["low:0x204"]?.decoded ?? {};
  $: steering = $latestById["low:0x201"]?.decoded ?? {};
  $: brake = $latestById["low:0x721"]?.decoded ?? {};
  $: motor = $latestById["low:0x206"]?.decoded ?? $latestById["high:0x206"]?.decoded ?? {};
  $: obstacle = $latestById["high:0x400"]?.decoded.distance_label ?? $latestById["high:0x400"]?.decoded.distance_mm ?? "--";

  $: estopActive = Boolean(safety.estop_active) || Boolean(diag.estop_active);
  $: mode = String(rtState.mode_name ?? diag.mode_name ?? "--");
  $: speed = firstValue(rtDrive.motor_speed_mmps, motor.actual_speed_mmps, $latestById["high:0x120"]?.decoded.speed_mmps, "--");
  $: gear = firstValue(rtDrive.gear_name, motor.gear_name, drive.gear_name, "--");
  $: steerAngle = firstValue(steering.str_angle, "--");
  $: brakeState = firstValue(brake.pressure_value, $latestById["high:0x301"]?.decoded.brake_pressure_kpa, "--");

  $: pairs = buildPairs($latestById);
  $: activeRows = ids
    .map((item) => ({ item, frame: $latestById[`${item.bus}:${item.id}`] }))
    .filter((row): row is { item: CanMessageDef; frame: CanFrame } => Boolean(row.frame))
    .sort((a, b) => frameStamp(b.frame) - frameStamp(a.frame))
    .slice(0, 16);

  function firstValue(...values: unknown[]): unknown {
    return values.find((value) => value !== undefined && value !== null && value !== "");
  }

  function frameStamp(frame?: CanFrame): number {
    return frame?.ts_real ?? frame?.ts ?? 0;
  }

  function activityState(bus: Bus): SignalState {
    const busStats = bus === "high" ? high : low;
    if (busStats.tec > 0 || busStats.rec > 0) return "warn";
    return busStats.active ? "ok" : "idle";
  }

  function pairState(command?: CanFrame, feedback?: CanFrame): SignalState {
    if (command && feedback) return "ok";
    if (command && !feedback) return "warn";
    if (!command && feedback) return "idle";
    return "idle";
  }

  function buildPairs(latest: Record<string, CanFrame>): PairRow[] {
    const rows: PairRow[] = [
      {
        label: "Drive pipeline",
        command: latest["high:0x300"],
        feedback: latest["low:0x204"] ?? latest["low:0x206"],
        commandLabel: "HOST 0x300",
        feedbackLabel: "RT/MTR 0x204/0x206",
        state: pairState(latest["high:0x300"], latest["low:0x204"] ?? latest["low:0x206"])
      },
      {
        label: "Steering",
        command: latest["low:0x169"],
        feedback: latest["low:0x201"],
        commandLabel: "VCU 0x169",
        feedbackLabel: "EPS-C 0x201",
        state: pairState(latest["low:0x169"], latest["low:0x201"])
      },
      {
        label: "Brake",
        command: latest["low:0x7B9"] ?? latest["high:0x301"] ?? latest["low:0x205"],
        feedback: latest["low:0x721"],
        commandLabel: "VCU/RT 0x7B9/0x301/0x205",
        feedbackLabel: "SEB 0x721",
        state: pairState(latest["low:0x7B9"] ?? latest["high:0x301"] ?? latest["low:0x205"], latest["low:0x721"])
      },
      {
        label: "Safety",
        command: latest["high:0x001"] ?? latest["low:0x001"],
        feedback: latest["high:0x011"] ?? latest["low:0x011"],
        commandLabel: "ESTOP 0x001",
        feedbackLabel: "SYS 0x011",
        state: estopActive ? "danger" : pairState(latest["high:0x001"] ?? latest["low:0x001"], latest["high:0x011"] ?? latest["low:0x011"])
      }
    ];
    return rows;
  }
</script>

<section class="ops-dashboard">
  <div class="summary-grid">
    <article class="status-tile" data-state={adapterOnline && linkOpen ? "ok" : "warn"}>
      <span>Adapter</span>
      <strong>{adapter}</strong>
      <small>{transport} / {linkOpen ? "link open" : "link closed"}</small>
    </article>
    <article class="status-tile" data-state={activityState("high")}>
      <span>High bus</span>
      <strong>{high.active ? "ACTIVE" : "QUIET"}</strong>
      <small>{Math.round(high.fps)} fps / {high.load_pct.toFixed(1)}% load / TEC {high.tec}</small>
    </article>
    <article class="status-tile" data-state={activityState("low")}>
      <span>Low bus</span>
      <strong>{low.active ? "ACTIVE" : "QUIET"}</strong>
      <small>{Math.round(low.fps)} fps / {low.load_pct.toFixed(1)}% load / TEC {low.tec}</small>
    </article>
    <article class="status-tile" data-state={estopActive ? "danger" : "ok"}>
      <span>Safety state</span>
      <strong>{estopActive ? "ESTOP" : "CLEAR"}</strong>
      <small>mode {mode} / WS {$wsConnected ? "live" : "idle"}</small>
    </article>
  </div>

  <div class="console-grid">
    <section class="panel system-panel">
      <div class="panel-title">
        <h2>Vehicle State</h2>
        <span>{ids.length} configured IDs</span>
      </div>
      <div class="state-grid">
        <div>
          <span>Mode</span>
          <strong>{mode}</strong>
        </div>
        <div>
          <span>Speed</span>
          <strong>{speed} <small>mm/s</small></strong>
        </div>
        <div>
          <span>Gear</span>
          <strong>{gear}</strong>
        </div>
        <div>
          <span>Steering</span>
          <strong>{steerAngle} <small>0.1 deg</small></strong>
        </div>
        <div>
          <span>Brake</span>
          <strong>{brakeState}</strong>
        </div>
        <div>
          <span>Obstacle</span>
          <strong>{obstacle}</strong>
        </div>
      </div>
    </section>

    <section class="panel">
      <div class="panel-title">
        <h2>Adapter Link</h2>
        <span>{$status.bridge?.bitrate ? `${$status.bridge.bitrate} bit/s` : "--"}</span>
      </div>
      <div class="link-table">
        <div><span>Backend</span><strong>{$status.backend_online ? "online" : "offline"}</strong></div>
        <div><span>Adapter</span><strong>{adapterOnline ? "online" : "offline"}</strong></div>
        <div><span>Transport</span><strong>{transport}</strong></div>
        <div><span>Last status</span><strong>{$status.last_status_at ? frameAge({ ts_real: $status.last_status_at } as CanFrame) : "--"}</strong></div>
        <div><span>High REC</span><strong>{high.rec}</strong></div>
        <div><span>Low REC</span><strong>{low.rec}</strong></div>
      </div>
    </section>
  </div>

  <section class="panel">
    <div class="panel-title">
      <h2>Command / Feedback</h2>
      <span>{pairs.filter((row) => row.command || row.feedback).length} active paths</span>
    </div>
    <div class="pair-table">
      <div class="pair-head">
        <span>Path</span>
        <span>Command</span>
        <span>Feedback</span>
        <span>Freshness</span>
      </div>
      {#each pairs as row}
        <div class="pair-row" data-state={row.state}>
          <strong>{row.label}</strong>
          <div>
            <span>{row.commandLabel}</span>
            <code>{row.command ? formatDecoded(row.command.decoded) : "--"}</code>
          </div>
          <div>
            <span>{row.feedbackLabel}</span>
            <code>{row.feedback ? formatDecoded(row.feedback.decoded) : "--"}</code>
          </div>
          <div class="freshness">
            <span>{row.feedback ? frameAge(row.feedback) : row.command ? frameAge(row.command) : "--"}</span>
          </div>
        </div>
      {/each}
    </div>
  </section>

  <section class="panel">
    <div class="panel-title">
      <h2>Recent Active Frames</h2>
      <span>{activeRows.length} shown</span>
    </div>
    <div class="frame-table">
      <div class="frame-head">
        <span>Time</span>
        <span>Bus</span>
        <span>ID</span>
        <span>Name</span>
        <span>Payload</span>
        <span>Decoded</span>
      </div>
      {#each activeRows as row}
        <div class="frame-row">
          <span class="mono">{frameTime(row.frame)}</span>
          <span class="bus-tag">{row.item.bus}</span>
          <span class="mono">{row.item.id}</span>
          <strong>{row.item.name}</strong>
          <span class="mono">{formatBytes(row.frame.data)}</span>
          <code>{formatDecoded(row.frame.decoded)}</code>
        </div>
      {:else}
        <div class="empty-state">No CAN frames have been received yet.</div>
      {/each}
    </div>
  </section>
</section>

<style>
  .ops-dashboard {
    display: grid;
    gap: 14px;
  }

  .summary-grid,
  .console-grid {
    display: grid;
    gap: 14px;
    grid-template-columns: repeat(4, minmax(0, 1fr));
  }

  .console-grid {
    grid-template-columns: minmax(0, 1.35fr) minmax(320px, 0.65fr);
  }

  .status-tile {
    background: var(--panel);
    border: 1px solid var(--panel-border);
    border-left: 4px solid var(--muted);
    border-radius: 6px;
    min-height: 96px;
    min-width: 0;
    padding: 14px;
  }

  .status-tile[data-state="ok"] { border-left-color: var(--ok); }
  .status-tile[data-state="warn"] { border-left-color: var(--warn); }
  .status-tile[data-state="danger"] { border-left-color: var(--err); }

  .status-tile span,
  .state-grid span,
  .link-table span,
  .pair-row span,
  .pair-head span,
  .frame-head span {
    color: var(--muted);
    display: block;
    font-size: 0.72rem;
    font-weight: 700;
    letter-spacing: 0.02em;
    text-transform: uppercase;
  }

  .status-tile strong {
    display: block;
    font-size: 1.35rem;
    margin-top: 10px;
  }

  .status-tile small {
    color: var(--muted);
    display: block;
    font-size: 0.78rem;
    margin-top: 6px;
  }

  .state-grid,
  .link-table {
    display: grid;
    gap: 8px;
    grid-template-columns: repeat(3, minmax(0, 1fr));
  }

  .link-table {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }

  .state-grid div,
  .link-table div {
    background: var(--bg);
    border: 1px solid var(--panel-border);
    border-radius: 6px;
    min-height: 72px;
    min-width: 0;
    padding: 12px;
  }

  .state-grid strong,
  .link-table strong {
    display: block;
    font-size: 1.1rem;
    margin-top: 8px;
    overflow-wrap: anywhere;
  }

  .state-grid small {
    color: var(--muted);
    font-size: 0.7rem;
    font-weight: 500;
  }

  .pair-table,
  .frame-table {
    display: grid;
    gap: 0;
    overflow: hidden;
    min-width: 0;
  }

  .pair-head,
  .pair-row {
    display: grid;
    gap: 12px;
    grid-template-columns: 150px minmax(0, 1fr) minmax(0, 1fr) 100px;
    min-height: 44px;
  }

  .pair-head,
  .frame-head {
    background: var(--bg);
    border-bottom: 1px solid var(--panel-border);
    padding: 10px 12px;
  }

  .pair-row {
    align-items: center;
    border-bottom: 1px solid var(--panel-border);
    border-left: 3px solid var(--muted);
    padding: 10px 12px;
  }

  .pair-head > *,
  .pair-row > *,
  .frame-head > *,
  .frame-row > * {
    min-width: 0;
    overflow-wrap: anywhere;
  }

  .pair-row[data-state="ok"] { border-left-color: var(--ok); }
  .pair-row[data-state="warn"] { border-left-color: var(--warn); }
  .pair-row[data-state="danger"] { border-left-color: var(--err); }

  .pair-row code,
  .frame-row code {
    color: var(--fg);
    display: block;
    font-family: "Cascadia Mono", "SFMono-Regular", Consolas, monospace;
    font-size: 0.78rem;
    margin-top: 4px;
    overflow-wrap: anywhere;
  }

  .freshness span {
    color: var(--fg);
    text-transform: none;
  }

  .frame-head,
  .frame-row {
    display: grid;
    gap: 10px;
    grid-template-columns: 112px 64px 72px minmax(150px, 0.4fr) minmax(160px, 0.45fr) minmax(260px, 1fr);
  }

  .frame-row {
    align-items: start;
    border-bottom: 1px solid var(--panel-border);
    min-height: 42px;
    padding: 9px 12px;
  }

  .frame-row strong,
  .frame-row span {
    font-size: 0.82rem;
  }

  .empty-state {
    color: var(--muted);
    padding: 18px 12px;
  }

  @media (max-width: 1100px) {
    .summary-grid,
    .console-grid {
      grid-template-columns: repeat(2, minmax(0, 1fr));
    }

    .system-panel {
      grid-column: span 2;
    }

    .pair-head,
    .pair-row,
    .frame-head,
    .frame-row {
      grid-template-columns: 1fr;
    }
  }

  @media (max-width: 680px) {
    .summary-grid,
    .console-grid,
    .state-grid,
    .link-table {
      grid-template-columns: 1fr;
    }

    .system-panel {
      grid-column: auto;
    }
  }
</style>
