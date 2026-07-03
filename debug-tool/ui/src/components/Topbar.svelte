<script lang="ts">
  import { frames, stats, status } from "../stores/can";
  import { telemetry, type Telemetry } from "../stores/telemetry";
  import type { Bus } from "../lib/can-decoder";
  import { sendFrame } from "../lib/api";
  import { logError } from "../stores/errors";

  // ── Props ──
  export let onReset: () => void;
  export let onRestart: () => void;
  export let onStop: () => void;

  // ── Health state ──
  function healthDot(ok: boolean, degraded = false): "ok" | "warn" | "bad" {
    if (ok) return "ok";
    return degraded ? "warn" : "bad";
  }

  function canBusDot(bus: Bus): "ok" | "warn" | "bad" {
    const s = $stats.buses[bus];
    if (s.active && s.fps > 0) return "ok";
    if (s.total > 0) return "warn";
    return "bad";
  }

  function canBusTooltip(bus: Bus): string {
    const s = $stats.buses[bus];
    return `${bus.toUpperCase()} CAN: ${Math.round(s.fps)} fps, ${s.total} frames, TEC ${s.tec}, REC ${s.rec}`;
  }

  function bridgeTooltip(): string {
    const b = $status.bridge;
    return [
      b?.adapter,
      b?.path,
      b?.bitrate ? `${b.bitrate} bit/s` : "",
      b?.last_error ? `Err: ${b.last_error}` : ""
    ].filter(Boolean).join(" / ") || "Bridge";
  }

  // ── Bulb state helpers ──
  $: t = $telemetry;

  let tick = 0;
  $: {
    if (t.leftTurn || t.rightTurn) {
      const interval = setInterval(() => tick++, 500);
    }
  }
  function flashOn(active: boolean): boolean {
    if (!active) return false;
    return tick % 2 === 0;
  }

  function gearColor(g: string | null): string {
    switch (g) {
      case "D": return "var(--ok)";
      case "R": return "var(--warn)";
      case "S": return "var(--accent)";
      default: return "var(--muted)";
    }
  }

  function safetyColor(s: string | null): string {
    switch (s) {
      case "Normal": return "var(--ok)";
      case "InternalEstop": return "var(--warn)";
      case "Fault": return "var(--err)";
      default: return "var(--muted)";
    }
  }

  // ── Vehicle command buttons ──
  let sending = false;

  const MODES = [
    { label: "MANUAL", value: 0, next: "AUTO" },
    { label: "AUTO", value: 1, next: "ESTOP" },
    { label: "ESTOP", value: 2, next: "MANUAL" },
  ] as const;

  function nextMode(): { label: string; value: number } {
    const cur = t.mode ?? "MANUAL";
    const m = MODES.find((x) => x.label === cur) ?? MODES[0];
    return MODES.find((x) => x.label === m.next) ?? MODES[0];
  }

  async function cycleMode() {
    if (sending) return;
    const nm = nextMode();
    sending = true;
    try {
      await sendFrame({ bus: "low", id: "0x110", dlc: 1, data: [nm.value] });
      logError("Mode set to " + nm.label);
    } catch (e) {
      logError("Mode change failed: " + (e instanceof Error ? e.message : String(e)));
    } finally {
      sending = false;
    }
  }

  async function toggleDcdc() {
    if (sending) return;
    sending = true;
    try {
      // Toggle: send enable=1 (on) — since we can't read current DCDC state from CAN,
      // default to "power on" semantics. Double-click semantics handled by user.
      await sendFrame({ bus: "low", id: "0x012", dlc: 1, data: [1] });
      logError("DCDC enable sent");
    } catch (e) {
      logError("DCDC command failed: " + (e instanceof Error ? e.message : String(e)));
    } finally {
      sending = false;
    }
  }

  async function sendEstop() {
    if (sending) return;
    if (!window.confirm("Send ESTOP frame? This will trigger emergency stop on all nodes.")) return;
    sending = true;
    try {
      await sendFrame({ bus: "low", id: "0x001", dlc: 0, data: [], confirm_estop: true });
      logError("ESTOP frame sent on low bus");
    } catch (e) {
      logError("ESTOP send failed: " + (e instanceof Error ? e.message : String(e)));
    } finally {
      sending = false;
    }
  }
</script>

<header class="topbar v2">
  <!-- ── Brand ── -->
  <div class="brand-compact">
    <span class="brand-name">E-Trike</span>
  </div>

  <!-- ── Health dots ── -->
  <div class="health-dots" aria-label="System health">
    <span class="hdot" data-state={healthDot(Boolean($status.backend_online))} title="Backend API" data-tooltip={$status.backend_online ? "API online" : "API offline"}>
      <svg class="hdot-svg" viewBox="0 0 16 16" width="13" height="13"><circle cx="5" cy="6" r="2.5" fill="none" stroke="currentColor" stroke-width="1.5"/><circle cx="11" cy="6" r="2.5" fill="none" stroke="currentColor" stroke-width="1.5"/><line x1="5" y1="9" x2="11" y2="9" stroke="currentColor" stroke-width="1.5"/><line x1="6" y1="13" x2="10" y2="13" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/></svg>
    </span>
    <span class="hdot" data-state={healthDot(Boolean($status.bridge?.connected), Boolean($status.backend_online))} title={bridgeTooltip()} data-tooltip={bridgeTooltip()}>
      <svg class="hdot-svg" viewBox="0 0 16 16" width="13" height="13"><rect x="2.5" y="1.5" width="11" height="7" rx="1.5" fill="none" stroke="currentColor" stroke-width="1.5"/><rect x="6" y="10" width="4" height="4.5" rx="1" fill="none" stroke="currentColor" stroke-width="1.5"/><line x1="8" y1="8.5" x2="8" y2="10" stroke="currentColor" stroke-width="1.5"/></svg>
    </span>
    <span class="hdot" data-state={canBusDot("high")} title={canBusTooltip("high")} data-tooltip={canBusTooltip("high")}>
      <span class="hdot-label">H</span>
    </span>
    <span class="hdot" data-state={canBusDot("low")} title={canBusTooltip("low")} data-tooltip={canBusTooltip("low")}>
      <span class="hdot-label">L</span>
    </span>
  </div>

  <span class="topbar-sep" aria-hidden="true"></span>

  <!-- ── Indicator bulbs ── -->
  <div class="bulb-strip" aria-label="Vehicle indicators">
    <span class="bulb turn-left" class:active={t.leftTurn} class:flash={flashOn(t.leftTurn)} title="Left turn signal" data-tooltip={t.leftTurn ? "Left turn ON" : "Left turn off"}>
      <span class="bulb-icon">&#x21E6;</span>
    </span>
    <span class="bulb turn-right" class:active={t.rightTurn} class:flash={flashOn(t.rightTurn)} title="Right turn signal" data-tooltip={t.rightTurn ? "Right turn ON" : "Right turn off"}>
      <span class="bulb-icon">&#x21E8;</span>
    </span>
    <span class="bulb brake" class:active={t.brakeLight} title="Brake light" data-tooltip={t.brakeLight ? "Brake light ON" : "Brake light off"}>
      <span class="bulb-icon">&#x25C9;</span>
    </span>
    <span class="bulb estop" class:active={t.estopActive} title="ESTOP" data-tooltip={t.estopActive ? "ESTOP ACTIVE" : "ESTOP clear"}>
      <span class="bulb-icon">&#x26D4;</span>
    </span>
  </div>

  <span class="topbar-sep" aria-hidden="true"></span>

  <!-- ── Telemetry readouts ── -->
  <div class="telemetry-strip" aria-label="Vehicle telemetry">
    <span class="telem-item speed" title="Motor speed">
      <span class="telem-icon">⚙</span>
      <span class="telem-val">{t.motorSpeedKmh !== null ? t.motorSpeedKmh.toFixed(1) : "--.-"}</span>
      <span class="telem-unit">km/h</span>
    </span>
    <span class="telem-item steer" title="Steering angle">
      <span class="telem-icon">↕</span>
      <span class="telem-val">{t.steerAngleDeg !== null ? (t.steerAngleDeg > 0 ? "+" : "") + t.steerAngleDeg.toFixed(1) : "--.-"}</span>
      <span class="telem-unit">°</span>
    </span>
    <span class="telem-item brake-bar" title="Brake pressure">
      <span class="telem-icon">⏚</span>
      <span class="brake-gauge">
        <span class="brake-fill" style="width: {Math.min((t.brakePressureMpa ?? 0) / 20 * 100, 100)}%"></span>
      </span>
      <span class="telem-val">{t.brakePressureMpa !== null ? t.brakePressureMpa.toFixed(1) : "--.-"}</span>
      <span class="telem-unit">MPa</span>
    </span>
    {#if t.gear}
      <span class="telem-item gear" title="Gear: {t.gear}" style="color: {gearColor(t.gear)}">
        <span class="gear-badge" style="border-color: {gearColor(t.gear)}">{t.gear}</span>
      </span>
    {/if}
    {#if t.safetyState}
      <span class="telem-item safety" title="Safety: {t.safetyState}" style="color: {safetyColor(t.safetyState)}">
        <span class="telem-val">{t.safetyState}</span>
      </span>
    {/if}
  </div>

  <span class="topbar-sep" aria-hidden="true"></span>

  <!-- ── Vehicle command buttons ── -->
  <div class="cmd-strip" aria-label="Vehicle commands">
    <button
      type="button"
      class="cmd-btn mode-btn"
      disabled={sending || !$status.bridge?.connected}
      on:click={cycleMode}
      title="Cycle mode: {nextMode().label}"
      data-tooltip="Next: {nextMode().label}"
    >
      <span class="cmd-icon">M</span>
      <span class="cmd-label">{t.mode ?? "MANUAL"}</span>
    </button>
    <button
      type="button"
      class="cmd-btn on-btn"
      disabled={sending || !$status.bridge?.connected}
      on:click={toggleDcdc}
      title="Power on / DCDC enable"
      data-tooltip="DCDC enable"
    >
      <span class="cmd-icon">&#x23FB;</span>
      <span class="cmd-label">ON</span>
    </button>
    <button
      type="button"
      class="cmd-btn estop-btn"
      disabled={sending || !$status.bridge?.connected}
      on:click={sendEstop}
      title="Send ESTOP (emergency stop)"
      data-tooltip="EMERGENCY STOP"
    >
      <span class="cmd-icon">&#x26D4;</span>
      <span class="cmd-label">ESTOP</span>
    </button>
  </div>

  <!-- ── Bridge action buttons ── -->
  <div class="action-strip compact" aria-label="Bridge actions">
    <button type="button" class="action-btn icon-only" data-testid="action-reset" on:click={onReset} title="Clear frames">
      <span aria-hidden="true">↺</span>
    </button>
    <button type="button" class="action-btn icon-only" data-testid="action-restart" on:click={onRestart} title="Restart bridge">
      <span aria-hidden="true">↻</span>
    </button>
    <button type="button" class="action-btn icon-only danger" data-testid="action-stop" on:click={onStop} title="Stop bridge">
      <span aria-hidden="true">■</span>
    </button>
  </div>
</header>
