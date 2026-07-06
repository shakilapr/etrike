<script lang="ts">
  import { stats, status } from "../stores/can";
  import { telemetry, ecuPresence } from "../stores/telemetry";
  import type { Bus } from "../lib/can-decoder";
  import { sendFrame, setMode as apiSetMode, type WorkModeConfig } from "../lib/api";
  import { logError } from "../stores/errors";
  import { workMode, modeLabel } from "../stores/work-mode";

  export let onReset: () => void;
  export let onRestart: () => void;
  export let onStop: () => void;

  const MODES: WorkModeConfig["mode"][] = ["full-sim", "emulator", "hybrid", "bench", "monitor"];

  async function switchMode(mode: WorkModeConfig["mode"]) {
    const defaults = await import("../lib/api").then(m => m.getModeDefaults());
    const config = (defaults as Record<string, WorkModeConfig>)[mode];
    if (!config) return;
    try {
      await apiSetMode(config);
      workMode.set(config);
    } catch (e) {
      logError("Mode switch: " + (e instanceof Error ? e.message : String(e)));
    }
  }

  // ── Health state helpers ──
  function hState(ok: boolean, degraded = false): "ok" | "warn" | "bad" {
    if (ok) return "ok"; return degraded ? "warn" : "bad";
  }
  function canState(bus: Bus): "ok" | "warn" | "bad" {
    const s = $stats.buses[bus];
    if (s.active && s.fps > 0) return "ok";
    return s.total > 0 ? "warn" : "bad";
  }
  function canTT(bus: Bus): string {
    const s = $stats.buses[bus];
    return bus.toUpperCase() + " CAN: " + Math.round(s.fps) + " fps, " + s.total + " frames";
  }
  function bridgeTT(): string {
    const b = $status.bridge;
    return [b?.adapter, b?.path, b?.bitrate ? b.bitrate + " bit/s" : "", b?.last_error ? "Err: " + b.last_error : ""].filter(Boolean).join(" / ") || "Bridge";
  }

  $: t = $telemetry;
  let tick = 0;
  $: { if (t.leftTurn || t.rightTurn) { const i = setInterval(() => tick++, 500); } }
  function flash(a: boolean): boolean { return a && tick % 2 === 0; }

  // ── Gear color ──
  function gColor(g: string | null): string {
    switch (g) { case "D": return "var(--ok)"; case "R": return "var(--warn)"; case "S": return "var(--accent)"; default: return "var(--muted)"; }
  }
  // ── Mode state ──
  function modeClass(m: string | null): string {
    switch (m) { case "MANUAL": return "manual"; case "AUTO": return "auto"; case "ESTOP": return "estop"; default: return "unknown"; }
  }
  function modeLabel(m: string | null): string {
    return m ?? "No mode";
  }
  // ── Safety color ──
  function sColor(s: string | null): string {
    switch (s) { case "Normal": return "var(--ok)"; case "InternalEstop": return "var(--warn)"; case "Fault": return "var(--err)"; default: return "var(--muted)"; }
  }

  // ── Vehicle commands ──
  let sending = false;
  function nextMode(): { label: string; value: number } {
    return t.mode === "AUTO" ? { label: "MANUAL", value: 0 } : { label: "AUTO", value: 1 };
  }
  async function cycleMode() {
    if (sending) return; sending = true;
    const nm = nextMode();
    try { await sendFrame({ bus: "low", id: "0x110", dlc: 1, data: [nm.value] }); logError("Mode → " + nm.label); }
    catch (e) { logError("Mode fail: " + (e instanceof Error ? e.message : String(e))); }
    finally { sending = false; }
  }
  async function toggleDcdc() {
    if (sending) return; sending = true;
    try { await sendFrame({ bus: "low", id: "0x012", dlc: 1, data: [1] }); logError("DCDC ON"); }
    catch (e) { logError("DCDC fail: " + (e instanceof Error ? e.message : String(e))); }
    finally { sending = false; }
  }
  async function sendEstop() {
    if (sending) return;
    if (!window.confirm("Send ESTOP? Emergency stop on all nodes.")) return;
    sending = true;
    try { await sendFrame({ bus: "low", id: "0x001", dlc: 0, data: [], confirm_estop: true }); logError("ESTOP sent"); }
    catch (e) { logError("ESTOP fail: " + (e instanceof Error ? e.message : String(e))); }
    finally { sending = false; }
  }

  const online = () => $status.bridge?.connected ?? false;

  type HealthKind = "api" | "usb" | "can" | "ecu" | "motor" | "steer" | "brake";
  type HealthItem = {
    group: "Link" | "Bus" | "ECU";
    label: string;
    value: string;
    kind: HealthKind;
    state: "ok" | "warn" | "bad";
    title: string;
  };

  const healthGroups = ["Link", "Bus", "ECU"] as const;

  function busValue(bus: Bus): string {
    const s = $stats.buses[bus];
    if (s.active && s.fps > 0) return Math.round(s.fps) + " fps";
    return s.total > 0 ? "seen" : "silent";
  }

  // Health bar — grouped like a vehicle diagnostic cluster: interface, CAN bus, ECUs.
  $: healthBar = [
    {
      group: "Link", label: "API", value: $status.backend_online ? "online" : "offline", kind: "api",
      state: hState(Boolean($status.backend_online)), title: $status.backend_online ? "Backend online" : "Backend offline"
    },
    {
      group: "Link", label: "USB", value: $status.bridge?.connected ? "linked" : "open", kind: "usb",
      state: hState(Boolean($status.bridge?.connected)), title: $status.bridge?.connected ? bridgeTT() : "Bridge disconnected"
    },
    { group: "Bus", label: "High", value: busValue("high"), kind: "can", state: canState("high"), title: canTT("high") },
    { group: "Bus", label: "Low",  value: busValue("low"),  kind: "can", state: canState("low"),  title: canTT("low") },
    {
      group: "ECU", label: "RT", value: $ecuPresence.rt ? "ready" : "lost", kind: "ecu",
      state: hState($ecuPresence.rt), title: $ecuPresence.rt ? "RT controller present" : "RT controller missing"
    },
    {
      group: "ECU", label: "SYS", value: $ecuPresence.sys ? "ready" : "lost", kind: "ecu",
      state: hState($ecuPresence.sys), title: $ecuPresence.sys ? "SYS controller present" : "SYS controller missing"
    },
    {
      group: "ECU", label: "MTR", value: $ecuPresence.mtr ? "ready" : "lost", kind: "motor",
      state: hState($ecuPresence.mtr), title: $ecuPresence.mtr ? "Motor controller present" : "Motor controller missing"
    },
    {
      group: "ECU", label: "SES", value: $ecuPresence.ses ? "ready" : "lost", kind: "steer",
      state: hState($ecuPresence.ses), title: $ecuPresence.ses ? "Steer-by-wire ECU present" : "Steer-by-wire ECU missing"
    },
    {
      group: "ECU", label: "SEB", value: $ecuPresence.seb ? "ready" : "lost", kind: "brake",
      state: hState($ecuPresence.seb), title: $ecuPresence.seb ? "Brake-by-wire ECU present" : "Brake-by-wire ECU missing"
    },
  ] satisfies HealthItem[];

  $: healthByGroup = healthGroups.map((group) => ({
    group,
    items: healthBar.filter((item) => item.group === group)
  }));

  $: ecuHealth = healthBar.filter((item) => item.group === "ECU");
  $: ecuReady = ecuHealth.filter((item) => item.state === "ok").length;
</script>

<!-- ═══════════════════════════════════════════════════════════════════ -->
<!-- Topbar — two-row: row-1=brand/health/indicators/state               -->
<!--                    row-2=telemetry/commands/actions                  -->
<!-- ═══════════════════════════════════════════════════════════════════ -->
<header class="topbar v3">
  <!-- ── Row 1 ── -->
  <div class="tb-row tb-row-main">
    <!-- Brand + mode selector -->
    <div class="tb-brand">
      <span>E-Trike</span>
      <select class="tb-mode-select" value={$workMode.mode} on:change={(e) => switchMode(e.currentTarget.value as WorkModeConfig["mode"])}>
        {#each MODES as m}
          <option value={m}>{modeLabel(m)}</option>
        {/each}
      </select>
    </div>

    <!-- Indicators — automotive-standard shapes, fixed size -->
    <div class="tb-indicators">
      <span class="tbi turn-l" class:on={t.leftTurn} class:flash={flash(t.leftTurn)} title="Left turn">
        <svg width="14" height="14" viewBox="0 0 20 16"><polygon points="18,3 2,8 18,13" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round"/></svg>
        <span>L</span>
      </span>
      <span class="tbi turn-r" class:on={t.rightTurn} class:flash={flash(t.rightTurn)} title="Right turn">
        <svg width="14" height="14" viewBox="0 0 20 16"><polygon points="2,3 18,8 2,13" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round"/></svg>
        <span>R</span>
      </span>
      <span class="tbi brake-i" class:on={t.brakeLight} title="Brake">
        <svg width="13" height="13" viewBox="0 0 16 16"><circle cx="8" cy="8" r="6" fill="none" stroke="currentColor" stroke-width="1.5"/><text x="8" y="12" text-anchor="middle" font-size="8" font-weight="900" fill="currentColor">!</text></svg>
        <span>BRK</span>
      </span>
      <!-- ESTOP: ISO 13850 emergency stop symbol -->
      <span class="tbi estop-i" class:on={t.estopActive} title={t.estopActive ? "ESTOP ACTIVE" : "ESTOP clear"}>
        <svg width="16" height="16" viewBox="0 0 24 24">
          <polygon points="12,1 22,6 22,18 12,23 2,18 2,6" fill="currentColor" opacity="0.12"/>
          <polygon points="12,2 21,6.5 21,17.5 12,22 3,17.5 3,6.5" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linejoin="round"/>
          <text x="12" y="16" text-anchor="middle" font-size="10" font-weight="900" fill="currentColor">!</text>
        </svg>
        <span>STOP</span>
      </span>
    </div>

    <span class="tb-sep"></span>

    <!-- Vehicle state: gear + mode — fixed width badges -->
    <div class="tb-vstate">
      <span class="tvs-gear" style="color:{gColor(t.gear)};border-color:{gColor(t.gear)}"><em>Gear</em><strong>{t.gear ?? "--"}</strong></span>
      <span class="tvs-mode {modeClass(t.mode)}"><em>Mode</em><strong>{modeLabel(t.mode)}</strong></span>
      {#if t.safetyState && t.safetyState !== "Normal"}
        <span class="tvs-safety" style="color:{sColor(t.safetyState)}"><em>Safety</em><strong>{t.safetyState}</strong></span>
      {/if}
    </div>
  </div>

  <!-- Health bar: diagnostic groups mirror vehicle bring-up checks. -->
  <div class="tb-health-row">
    <div class="tb-health" aria-label="System health">
      {#each healthByGroup as section}
        <span class="tbh-group" class:tbh-group-ecu={section.group === "ECU"} aria-label={section.group + " health"}>
          <span class="tbh-group-label">{section.group}</span>
          {#if section.group === "ECU"}
            <span class="tbh-rollup" data-state={ecuReady === ecuHealth.length ? "ok" : ecuReady > 0 ? "warn" : "bad"} title="ECU presence summary">
              <span>{ecuReady}/{ecuHealth.length}</span>
              <strong>ready</strong>
            </span>
            <span class="ecu-dots" aria-label="ECU detail">
              {#each section.items as h}
                <span class="ecu-dot" data-state={h.state} title={h.title}>
                  <span>{h.label}</span>
                </span>
              {/each}
            </span>
          {:else}
            {#each section.items as h}
              <span class="tbh" data-state={h.state} title={h.title}>
                <span class="tbh-icon" aria-hidden="true">
                  {#if h.kind === "api"}
                    <svg viewBox="0 0 24 24"><path d="M5 7h14v10H5z"/><path d="M8 20h8M12 17v3"/><path d="M8.5 11h.01M12 11h.01M15.5 11h.01"/></svg>
                  {:else if h.kind === "usb"}
                    <svg viewBox="0 0 24 24"><path d="M12 3v12"/><path d="M8 7l4-4 4 4"/><path d="M7 13a3 3 0 0 0 3 3h2"/><path d="M17 13a3 3 0 0 1-3 3h-2"/><path d="M18 10v4h-2v-4z"/></svg>
                  {:else}
                    <svg viewBox="0 0 24 24"><path d="M4 8h16v8H4z"/><path d="M7 8V5m10 3V5M7 19v-3m10 3v-3"/><path d="M8 12h.01M12 12h.01M16 12h.01"/></svg>
                  {/if}
                </span>
                <span class="tbh-copy">
                  <em>{h.label}</em>
                  <strong>{h.value}</strong>
                </span>
              </span>
            {/each}
          {/if}
        </span>
      {/each}
    </div>
  </div>

  <!-- ── Row 2 ── -->
  <div class="tb-row">
    <!-- Telemetry — fixed-width readouts -->
    <div class="tb-telem">
      <span class="tbt speed" class:noData={t.motorSpeedKmh === null} title="Motor speed"><em>Speed</em> <strong>{t.motorSpeedKmh !== null ? t.motorSpeedKmh.toFixed(1) : "No data"}</strong> <u>km/h</u></span>
      <span class="tbt steer" class:noData={t.steerAngleDeg === null} title="Steering angle"><em>Steer</em> <strong>{t.steerAngleDeg !== null ? (t.steerAngleDeg > 0 ? "+" : "") + t.steerAngleDeg.toFixed(1) : "No data"}</strong> <u>deg</u></span>
      <span class="tbt brake" class:noData={t.brakePressureMpa === null} title="Brake pressure">
        <em>Brake</em>
        <span class="tbt-gauge"><span class="tbt-fill" style="width:{Math.min((t.brakePressureMpa ?? 0) / 20 * 100, 100)}%"></span></span>
        <strong>{t.brakePressureMpa !== null ? t.brakePressureMpa.toFixed(1) : "No data"}</strong> <u>MPa</u>
      </span>
    </div>

    <!-- Vehicle commands -->
    <div class="tb-cmds">
      <span class="tb-group-label">Vehicle</span>
      <button class="tb-btn" disabled={sending || !online()} on:click={cycleMode} title="Toggle MANUAL/AUTO">
        <svg width="13" height="13" viewBox="0 0 16 16"><path d="M2 8a6 6 0 0110.47-4" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/><path d="M14 8a6 6 0 01-10.47 4" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/><polyline points="11.5,1.5 12.5,4 10,4" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/></svg>
      </button>
      <button class="tb-btn" disabled={sending || !online()} on:click={toggleDcdc} title="DCDC power">
        <svg width="13" height="13" viewBox="0 0 16 16"><path d="M6 2v5H3l6 7v-5h3L6 2z" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linejoin="round"/></svg>
      </button>
      <button class="tb-btn estop" disabled={sending || !online()} on:click={sendEstop} title="Emergency stop">
        <svg width="13" height="13" viewBox="0 0 16 16"><rect x="1.5" y="1.5" width="13" height="13" rx="2" fill="none" stroke="currentColor" stroke-width="1.5"/><line x1="5" y1="5" x2="11" y2="11" stroke="currentColor" stroke-width="2" stroke-linecap="round"/><line x1="11" y1="5" x2="5" y2="11" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg>
      </button>
    </div>

    <span class="tb-sep"></span>

    <!-- Bridge actions -->
    <div class="tb-actions">
      <span class="tb-group-label">Bridge</span>
      <button class="tb-btn" on:click={onReset} title="Clear frames">
        <svg width="13" height="13" viewBox="0 0 16 16"><path d="M2 4v2h2" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/><path d="M4 6a6 6 0 111.5 4.5" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>
      </button>
      <button class="tb-btn" on:click={onRestart} title="Restart bridge">
        <svg width="13" height="13" viewBox="0 0 16 16"><path d="M1 4v4h4" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/><path d="M3.5 12A6 6 0 0014 6" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>
      </button>
      <button class="tb-btn danger" on:click={onStop} title="Stop bridge">
        <svg width="13" height="13" viewBox="0 0 16 16"><rect x="3" y="3" width="10" height="10" rx="2" fill="none" stroke="currentColor" stroke-width="1.8"/></svg>
      </button>
    </div>
  </div>
</header>
