<script lang="ts">
  import { onMount } from "svelte";
  import CanInjector from "./components/CanInjector.svelte";
  import CanMonitor from "./components/CanMonitor.svelte";
  import Controller from "./components/Controller.svelte";
  import Dashboard from "./components/Dashboard.svelte";
  import PipelineView from "./components/PipelineView.svelte";
  import Stats from "./components/Stats.svelte";
  import UnitTest from "./components/UnitTest.svelte";
  import { clearFrames, getCanIds, getFrames, getStats, getStatus, getTemplates, restartBridge, stopBridge, type BackendStatus } from "./lib/api";
  import type { Bus, CanMessageDef, InjectionTemplate } from "./lib/can-decoder";
  import { connectStream, type StreamHandle } from "./lib/ws";
  import { frames, ingestInitialFrames, ingestMessage, stats, status, wsConnected } from "./stores/can";
  import { errorLog, logError } from "./stores/errors";
  import { heldKeys, kbBus, kbEvent, type KbAction } from "./stores/keyboard";

  type Tab = "dashboard" | "monitor" | "injector" | "controller" | "unit-test" | "pipeline" | "stats";

  let activeTab: Tab = "dashboard";
  let ids: CanMessageDef[] = [];
  let templates: InjectionTemplate[] = [];
  let loadError = "";
  let stream: StreamHandle | null = null;

  const tabs: Array<{ id: Tab; label: string }> = [
    { id: "dashboard", label: "Dashboard" },
    { id: "monitor", label: "CAN Monitor" },
    { id: "injector", label: "Injector" },
    { id: "controller", label: "Controller" },
    { id: "unit-test", label: "Unit Test" },
    { id: "pipeline", label: "Pipeline" },
    { id: "stats", label: "Statistics" }
  ];

  // ── Transport source label ──
  function transportLabel(): string {
    const t = $status.bridge?.transport;
    switch (t) {
      case "canalystii": return "CANalyst-II";
      case "serial": return "ESP32 Serial";
      case "mqtt": return "MQTT";
      case "disabled": return "Disabled";
      default: return t ?? "Unknown";
    }
  }

  function transportShortLabel(): string {
    const t = $status.bridge?.transport;
    switch (t) {
      case "canalystii": return "CANalyst";
      case "serial": return "Serial";
      case "mqtt": return "MQTT";
      case "disabled": return "Disabled";
      default: return t ?? "Unknown";
    }
  }

  function healthState(ok: boolean, degraded = false): "ok" | "warn" | "bad" {
    if (ok) return "ok";
    return degraded ? "warn" : "bad";
  }

  function busDetectionLabel(): string {
    const bd = $status.bus_detection;
    if (!bd?.detected || bd.confidence === "none") return "Unconfirmed";
    const suffix = bd.confidence === "low" ? " low confidence" : "";
    return `${bd.bus.toUpperCase()}${suffix}`;
  }

  function bridgeLabel(): string {
    const linkOpen = Boolean($status.bridge?.link_open || $status.serial?.port_open);
    return `${transportShortLabel()} / ${linkOpen ? "Open" : "Closed"}`;
  }

  function canBusState(bus: Bus): "ok" | "warn" | "bad" {
    if (!$status.backend_online) return "bad";
    const linkOpen = Boolean($status.bridge?.link_open || $status.serial?.port_open);
    const busStats = bus === "high" ? $stats.buses.high : $stats.buses.low;
    if (busStats.active && busStats.fps > 0) return "ok";
    if (linkOpen || busStats.total > 0) return "warn";
    return "bad";
  }

  function canBusLabel(bus: Bus): string {
    const busStats = bus === "high" ? $stats.buses.high : $stats.buses.low;
    if (busStats.active && busStats.fps > 0) return `${Math.round(busStats.fps)} fps`;
    if (busStats.total > 0) return "Quiet";
    return "No traffic";
  }

  function bridgeTitle(): string {
    const bridge = $status.bridge;
    const parts = [
      bridge?.adapter,
      bridge?.path,
      bridge?.bitrate ? `${bridge.bitrate} bit/s` : "",
      bridge?.last_error ? `Last error: ${bridge.last_error}` : ""
    ].filter(Boolean);
    return parts.join(" / ") || "CAN bridge status";
  }

  function backendTitle(): string {
    return $status.backend_online ? "Backend API is responding" : "Backend API is offline";
  }

  function highCanTitle(): string {
    const bus = $stats.buses.high;
    return `High CAN channel: ${Math.round(bus.fps)} fps, ${bus.total} frames, TEC ${bus.tec}, REC ${bus.rec}`;
  }

  function lowCanTitle(): string {
    const bus = $stats.buses.low;
    return `Low CAN channel: ${Math.round(bus.fps)} fps, ${bus.total} frames, TEC ${bus.tec}, REC ${bus.rec}. Detection: ${busDetectionLabel()}`;
  }

  // ── Keyboard controls ──
  // Layer 1: held-keys state map — raw, no input-filtering.
  // Tracks which physical keys are down so the Controller can
  // poll them each tick (game-loop pattern).
  function trackKey(e: KeyboardEvent, down: boolean) {
    const k = e.key.toLowerCase();
    if (["w","s","a","d","b"].includes(k)) {
      e.preventDefault();
      heldKeys.update(set => {
        const next = new Set(set);
        down ? next.add(k) : next.delete(k);
        return next;
      });
    }
  }

  function onKeyDown(e: KeyboardEvent) { trackKey(e, true); }
  function onKeyUp(e: KeyboardEvent)   { trackKey(e, false); }

  // Safety: if the window loses focus while keys are held, clear everything.
  function onBlur() {
    heldKeys.set(new Set());
  }

  // Layer 2: discrete actions (Tab / Esc / Space×2).  Filter input elements
  // so typing in forms doesn't trigger vehicle commands.
  let lastSpaceTs = 0;

  function handleDiscrete(e: KeyboardEvent) {
    if (e.target instanceof HTMLInputElement || e.target instanceof HTMLSelectElement || e.target instanceof HTMLTextAreaElement) return;

    let action: KbAction | null = null;
    let bus: Bus | null = null;

    switch (e.key) {
      case "Escape": action = { type: "zero_all" }; break;
      case "Tab":
        e.preventDefault();
        kbBus.update((b) => b === "high" ? "low" : "high");
        return;
      case " ":
        e.preventDefault();
        const now = Date.now();
        if (now - lastSpaceTs < 1000) {
          action = { type: "estop_send" };
          lastSpaceTs = 0;
        } else {
          action = { type: "estop_confirm" };
          lastSpaceTs = now;
        }
        break;
    }

    if (action) {
      kbBus.subscribe((b) => bus = b)();
      kbEvent.set({ action, bus: bus!, ts: Date.now() });
    }
  }

  onMount(() => {
    void bootstrap();
    const timer = window.setInterval(refreshStatus, 3000);
    window.addEventListener("keydown", onKeyDown);
    window.addEventListener("keyup", onKeyUp);
    window.addEventListener("keydown", handleDiscrete);
    window.addEventListener("blur", onBlur);

    return () => {
      window.clearInterval(timer);
      stream?.close();
      window.removeEventListener("keydown", onKeyDown);
      window.removeEventListener("keyup", onKeyUp);
      window.removeEventListener("keydown", handleDiscrete);
      window.removeEventListener("blur", onBlur);
    };
  });

  async function bootstrap() {
    const errors: string[] = [];
    try {
      const [statusR, idsR, framesR, statsR, templatesR] = await Promise.allSettled([
        getStatus(),
        getCanIds(),
        getFrames(),
        getStats(),
        getTemplates()
      ]);

      if (statusR.status === "fulfilled") status.set(statusR.value);
      else errors.push(`status: ${String(statusR.reason)}`);

      if (idsR.status === "fulfilled") ids = idsR.value;
      else errors.push(`ids: ${String(idsR.reason)}`);

      if (framesR.status === "fulfilled") ingestInitialFrames(framesR.value);
      else errors.push(`frames: ${String(framesR.reason)}`);

      if (statsR.status === "fulfilled") stats.set(statsR.value);
      else errors.push(`stats: ${String(statsR.reason)}`);

      if (templatesR.status === "fulfilled") templates = templatesR.value;
      else errors.push(`templates: ${String(templatesR.reason)}`);

      loadError = errors.length > 0 ? errors.join("; ") : "";
      for (const e of errors) logError(e);
    } catch (error) {
      const msg = error instanceof Error ? error.message : String(error);
      loadError = msg;
      logError(msg);
    }

    // Connect WebSocket AFTER initial REST data loads so WS frames don't get wiped
    stream = connectStream(ingestMessage, (connected) => wsConnected.set(connected));
  }

  async function refreshStatus() {
    try {
      const payload: BackendStatus = await getStatus();
      status.set(payload);
    } catch (error) {
      const msg = error instanceof Error ? error.message : String(error);
      status.update((current) => ({
        ...current,
        backend_online: false,
        adapter_connected: false,
        esp32_connected: false,
        warning: msg
      }));
      logError("Status poll: " + msg);
    }
  }

  async function resetFrames() {
    if (!window.confirm("Clear all stored CAN frames? This cannot be undone.")) return;
    try {
      await clearFrames();
      frames.set([]);
      loadError = "";
    } catch (err) {
      const msg = "Reset failed: " + (err instanceof Error ? err.message : String(err));
      loadError = msg;
      logError(msg);
    }
  }

  async function restartBridgeHandler() {
    if (!window.confirm("Restart the bridge connection? This will disconnect and reconnect the CAN transport.")) return;
    try {
      await restartBridge();
      loadError = "";
    } catch (err) {
      const msg = "Restart failed: " + (err instanceof Error ? err.message : String(err));
      loadError = msg;
      logError(msg);
    }
  }

  async function stopBridgeHandler() {
    if (!window.confirm("Stop the bridge connection? The backend stays online but CAN traffic will stop.")) return;
    try {
      await stopBridge();
      loadError = "";
    } catch (err) {
      const msg = "Stop failed: " + (err instanceof Error ? err.message : String(err));
      loadError = msg;
      logError(msg);
    }
  }
</script>

<div class="app-shell">
  <header class="topbar">
    <div class="brand-block">
      <p class="eyebrow">Dual CAN Bus Bench Tool</p>
      <h1>E-Trike Debug</h1>
    </div>
    <div class="status-strip health-strip" aria-label="System health">
      <span class="health-item" data-state={healthState(Boolean($status.backend_online))} data-testid="health-backend" title={backendTitle()} data-tooltip={backendTitle()}>
        <span class="health-dot"></span>
        <span class="health-icon" aria-hidden="true">API</span>
        <span class="health-label">Backend</span>
        <strong>{$status.backend_online ? "Online" : "Offline"}</strong>
      </span>
      <span class="health-item" data-state={healthState(Boolean($status.bridge?.connected), Boolean($status.backend_online))} data-testid="health-bridge" title={bridgeTitle()} data-tooltip={bridgeTitle()}>
        <span class="health-dot"></span>
        <span class="health-icon" aria-hidden="true">USB</span>
        <span class="health-label">Bridge</span>
        <strong>{bridgeLabel()}</strong>
      </span>
      <span class="health-item" data-state={canBusState("high")} data-testid="health-high-can" title={highCanTitle()} data-tooltip={highCanTitle()}>
        <span class="health-dot"></span>
        <span class="health-icon" aria-hidden="true">H</span>
        <span class="health-label">High CAN</span>
        <strong>{canBusLabel("high")}</strong>
      </span>
      <span class="health-item" data-state={canBusState("low")} data-testid="health-low-can" title={lowCanTitle()} data-tooltip={lowCanTitle()}>
        <span class="health-dot"></span>
        <span class="health-icon" aria-hidden="true">L</span>
        <span class="health-label">Low CAN</span>
        <strong>{canBusLabel("low")}</strong>
      </span>
    </div>
    <div class="action-strip" aria-label="Bridge actions">
      <button type="button" class="action-btn" data-testid="action-reset" on:click={resetFrames} title="Clear all stored CAN frames">
        <span aria-hidden="true">↺</span>
        <span>Reset</span>
      </button>
      <button type="button" class="action-btn" data-testid="action-restart" on:click={restartBridgeHandler} title="Restart the CAN bridge connection">
        <span aria-hidden="true">↻</span>
        <span>Restart</span>
      </button>
      <button type="button" class="action-btn danger" data-testid="action-stop" on:click={stopBridgeHandler} title="Stop the CAN bridge connection">
        <span aria-hidden="true">■</span>
        <span>Stop</span>
      </button>
    </div>
  </header>

  <nav class="tabs" aria-label="Debug views">
    {#each tabs as tab}
      <button class:active={activeTab === tab.id} type="button" on:click={() => (activeTab = tab.id)}>
        {tab.label}
      </button>
    {/each}
  </nav>

  {#if loadError}
    <section class="alert">{loadError}</section>
  {/if}

  <main>
    {#if activeTab === "dashboard"}
      <Dashboard {ids} />
    {:else if activeTab === "monitor"}
      <CanMonitor {ids} />
    {:else if activeTab === "injector"}
      <CanInjector {ids} {templates} />
    {:else if activeTab === "controller"}
      <Controller />
    {:else if activeTab === "unit-test"}
      <UnitTest {ids} />
    {:else if activeTab === "pipeline"}
      <PipelineView />
    {:else}
      <Stats {ids} />
    {/if}
  </main>

  {#if $errorLog.length > 0}
    <section class="error-log">
      <details>
        <summary>Error Log ({$errorLog.length})</summary>
        <div class="log-entries">
          {#each $errorLog as entry}
            <div class="log-entry">
              <span class="log-time">{new Date(entry.ts * 1000).toLocaleTimeString()}</span>
              <span class="log-msg">{entry.message}</span>
            </div>
          {/each}
        </div>
      </details>
    </section>
  {/if}
</div>
