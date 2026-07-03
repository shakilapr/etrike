<script lang="ts">
  import { stats, status } from "../stores/can";
  import { telemetry, ecuPresence } from "../stores/telemetry";
  import type { Bus } from "../lib/can-decoder";
  import { sendFrame } from "../lib/api";
  import { logError } from "../stores/errors";

  export let onReset: () => void;
  export let onRestart: () => void;
  export let onStop: () => void;

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
  // ── Mode color ──
  function mColor(m: string | null): string {
    switch (m) { case "MANUAL": return "var(--ok)"; case "AUTO": return "var(--accent)"; case "ESTOP": return "var(--err)"; default: return "var(--muted)"; }
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

  // Health bar — each item auto-computes its ok/bad state from reactive stores
  $: healthBar = [
    { label: "API",  ok: Boolean($status.backend_online),               okTT: "Backend online",         badTT: "Backend offline" },
    { label: "USB",  ok: Boolean($status.bridge?.connected),            okTT: bridgeTT(),               badTT: "Bridge disconnected" },
    { label: "RT",   ok: $ecuPresence.rt,                               okTT: "RT controller present",  badTT: "RT missing" },
    { label: "SYS",  ok: $ecuPresence.sys,                              okTT: "SYS controller present", badTT: "SYS missing" },
    { label: "MTR",  ok: $ecuPresence.mtr,                              okTT: "Motor ECU present",      badTT: "Motor missing" },
    { label: "SES",  ok: $ecuPresence.ses,                              okTT: "Steering ECU present",   badTT: "Steering missing" },
    { label: "SEB",  ok: $ecuPresence.seb,                              okTT: "Brake ECU present",      badTT: "Brake missing" },
  ];
</script>

<!-- ═══════════════════════════════════════════════════════════════════ -->
<!-- Topbar — two-row: row-1=brand/health/indicators/state               -->
<!--                    row-2=telemetry/commands/actions                  -->
<!-- ═══════════════════════════════════════════════════════════════════ -->
<header class="topbar v3">
  <!-- ── Row 1 ── -->
  <div class="tb-row">
    <!-- Brand -->
    <div class="tb-brand">
      <svg width="16" height="16" viewBox="0 0 24 24"><path d="M4 12l4-8h8l4 8-8 8z" fill="none" stroke="var(--accent)" stroke-width="2" stroke-linejoin="round"/></svg>
      <span>E-Trike</span>
    </div>

    <!-- Health bar: each ECU is one entry in the array, green=present, red=missing -->
    <div class="tb-health">
      {#each healthBar as h}
        <span class="tbh" data-state={h.ok ? 'ok' : 'bad'} title={h.ok ? h.okTT : h.badTT}>
          <em>{h.label}</em>
        </span>
      {/each}
    </div>

    <span class="tb-sep"></span>

    <!-- Indicators — automotive-standard shapes, fixed size -->
    <div class="tb-indicators">
      <span class="tbi turn-l" class:on={t.leftTurn} class:flash={flash(t.leftTurn)} title="Left turn">
        <svg width="14" height="14" viewBox="0 0 20 16"><polygon points="18,3 2,8 18,13" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round"/></svg>
      </span>
      <span class="tbi turn-r" class:on={t.rightTurn} class:flash={flash(t.rightTurn)} title="Right turn">
        <svg width="14" height="14" viewBox="0 0 20 16"><polygon points="2,3 18,8 2,13" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round"/></svg>
      </span>
      <span class="tbi brake-i" class:on={t.brakeLight} title="Brake">
        <svg width="13" height="13" viewBox="0 0 16 16"><circle cx="8" cy="8" r="6" fill="none" stroke="currentColor" stroke-width="1.5"/><text x="8" y="12" text-anchor="middle" font-size="8" font-weight="900" fill="currentColor">!</text></svg>
      </span>
      <!-- ESTOP: ISO 13850 emergency stop symbol -->
      <span class="tbi estop-i" class:on={t.estopActive} title={t.estopActive ? "ESTOP ACTIVE" : "ESTOP clear"}>
        <svg width="16" height="16" viewBox="0 0 24 24">
          <polygon points="12,1 22,6 22,18 12,23 2,18 2,6" fill="currentColor" opacity="0.12"/>
          <polygon points="12,2 21,6.5 21,17.5 12,22 3,17.5 3,6.5" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linejoin="round"/>
          <text x="12" y="16" text-anchor="middle" font-size="10" font-weight="900" fill="currentColor">!</text>
        </svg>
      </span>
    </div>

    <span class="tb-sep"></span>

    <!-- Vehicle state: gear + mode — fixed width badges -->
    <div class="tb-vstate">
      <span class="tvs-gear" style="color:{gColor(t.gear)};border-color:{gColor(t.gear)}">{t.gear ?? "?"}</span>
      <span class="tvs-mode" style="color:{mColor(t.mode)};background:{mColor(t.mode)}">{t.mode ?? "--"}</span>
      {#if t.safetyState && t.safetyState !== "Normal"}
        <span class="tvs-safety" style="color:{sColor(t.safetyState)}">{t.safetyState}</span>
      {/if}
    </div>
  </div>

  <!-- ── Row 2 ── -->
  <div class="tb-row">
    <!-- Telemetry — fixed-width readouts -->
    <div class="tb-telem">
      <span class="tbt speed" title="Motor speed"><em>Speed</em> <strong>{t.motorSpeedKmh !== null ? t.motorSpeedKmh.toFixed(1) : "--.-"}</strong> <u>km/h</u></span>
      <span class="tbt steer" title="Steering angle"><em>Steer</em> <strong>{t.steerAngleDeg !== null ? (t.steerAngleDeg > 0 ? "+" : "") + t.steerAngleDeg.toFixed(1) : "--.-"}</strong> <u>°</u></span>
      <span class="tbt brake" title="Brake pressure">
        <em>Brake</em>
        <span class="tbt-gauge"><span class="tbt-fill" style="width:{Math.min((t.brakePressureMpa ?? 0) / 20 * 100, 100)}%"></span></span>
        <strong>{t.brakePressureMpa !== null ? t.brakePressureMpa.toFixed(1) : "--.-"}</strong> <u>MPa</u>
      </span>
    </div>

    <!-- Vehicle commands -->
    <div class="tb-cmds">
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
