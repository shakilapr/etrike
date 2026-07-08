<script lang="ts">
  import { onMount } from "svelte";
  import CanDictionary from "./components/CanDictionary.svelte";
  import CanInjector from "./components/CanInjector.svelte";
  import CanMonitor from "./components/CanMonitor.svelte";
  import Controller from "./components/Controller.svelte";
  import Dashboard from "./components/Dashboard.svelte";
  import Emulator from "./components/Emulator.svelte";
  import PipelineView from "./components/PipelineView.svelte";
  import Stats from "./components/Stats.svelte";
  import Terminal from "./components/Terminal.svelte";
  import Topbar from "./components/Topbar.svelte";
  import TrikeViz from "./components/TrikeViz.svelte";
  import UnitTest from "./components/UnitTest.svelte";
  import { clearFrames, getCanIds, getFrames, getStats, getStatus, getTemplates, restartBridge, stopBridge, type BackendStatus } from "./lib/api";
  import type { Bus, CanMessageDef, InjectionTemplate } from "./lib/can-decoder";
  import { connectStream, type StreamHandle } from "./lib/ws";
  import { frames, ingestInitialFrames, ingestMessage, stats, status, wsConnected } from "./stores/can";
  import { errorLog, logError } from "./stores/errors";
  import { initFaultWatcher } from "./stores/faults";
  import { heldKeys, kbBus, kbEvent, type KbAction } from "./stores/keyboard";

  type Tab = "dashboard" | "monitor" | "dictionary" | "injector" | "controller" | "unit-test" | "pipeline" | "stats" | "terminal" | "emulator";

  let activeTab: Tab = "dashboard";
  let sidebarOpen = true;
  let ids: CanMessageDef[] = [];
  let templates: InjectionTemplate[] = [];
  let loadError = "";
  let stream: StreamHandle | null = null;
  let streamConnected = false;

  const tabs: Array<{ id: Tab; label: string }> = [
    { id: "dashboard", label: "Dashboard" },
    { id: "monitor", label: "CAN Monitor" },
    { id: "dictionary", label: "CAN Dictionary" },
    { id: "injector", label: "Injector" },
    { id: "controller", label: "Controller" },
    { id: "unit-test", label: "Unit Test" },
    { id: "pipeline", label: "Pipeline" },
    { id: "stats", label: "Statistics" },
    { id: "terminal", label: "Terminal" },
    { id: "emulator", label: "Emulator" }
  ];

  // (topbar helpers moved to Topbar.svelte)

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
    const unsubFaults = initFaultWatcher();
    window.addEventListener("keydown", onKeyDown);
    window.addEventListener("keyup", onKeyUp);
    window.addEventListener("keydown", handleDiscrete);
    window.addEventListener("blur", onBlur);

    return () => {
      window.clearInterval(timer);
      unsubFaults();
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
    stream = connectStream(ingestMessage, (connected) => {
      streamConnected = connected;
      wsConnected.set(connected);
    });
  }

  async function refreshStatus() {
    if (streamConnected) return;
    try {
      const payload: BackendStatus = await getStatus();
      if (payload.bus_stats) {
        stats.update((current) => ({ ...current, buses: payload.bus_stats }));
      }
      status.update((current) => ({
        ...current,
        ...payload,
        // Preserve WebSocket-derived bus_detection if fresh (<30s old),
        // otherwise allow REST to update (handles bridge restart).
        bus_detection: (current.bus_detection && (!current.bus_detection._ts || Date.now() - current.bus_detection._ts < 30_000))
          ? current.bus_detection : payload.bus_detection,
      }));
    } catch (error) {
      const msg = error instanceof Error ? error.message : String(error);
      status.update((current) => ({
        ...current,
        backend_online: false,
        adapter_connected: false,
        esp32_connected: false,
        bridge: {
          transport: current.bridge?.transport ?? "disabled",
          adapter: current.bridge?.adapter ?? "offline",
          connected: false,
          link_open: false,
          path: current.bridge?.path ?? null,
          baud_rate: current.bridge?.baud_rate ?? null,
          bitrate: current.bridge?.bitrate ?? null,
          last_status_at: current.bridge?.last_status_at ?? null,
          last_error: msg
        },
        warning: msg
      }));
      // Zero out stats so health bar reflects total failure
      stats.set({
        ts: Date.now() / 1000, uptime_s: 0,
        buses: { high: { active: false, total: 0, fps: 0, load_pct: 0, tec: 0, rec: 0, by_id: {} },
                 low:  { active: false, total: 0, fps: 0, load_pct: 0, tec: 0, rec: 0, by_id: {} } }
      });
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

<div class="app-shell" class:sidebar-open={sidebarOpen}>
  <Topbar onReset={resetFrames} onRestart={restartBridgeHandler} onStop={stopBridgeHandler} />

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

  <main class="content">
    <div style="display: {activeTab === 'dashboard' ? 'block' : 'none'}"><Dashboard {ids} /></div>
    <div style="display: {activeTab === 'monitor' ? 'block' : 'none'}"><CanMonitor {ids} /></div>
    <div style="display: {activeTab === 'dictionary' ? 'block' : 'none'}"><CanDictionary {ids} /></div>
    <div style="display: {activeTab === 'injector' ? 'block' : 'none'}"><CanInjector {ids} {templates} /></div>
    <div style="display: {activeTab === 'controller' ? 'block' : 'none'}"><Controller /></div>
    <div style="display: {activeTab === 'unit-test' ? 'block' : 'none'}"><UnitTest {ids} /></div>
    <div style="display: {activeTab === 'pipeline' ? 'block' : 'none'}"><PipelineView /></div>
    <div style="display: {activeTab === 'terminal' ? 'block' : 'none'}"><Terminal /></div>
    <div style="display: {activeTab === 'emulator' ? 'block' : 'none'}"><Emulator /></div>
    <div style="display: {activeTab === 'stats' ? 'block' : 'none'}"><Stats {ids} /></div>
  </main>

  <!-- Trike physics sidebar -->
  <button class="trike-sidebar-toggle" class:open={sidebarOpen} on:click={() => sidebarOpen = !sidebarOpen} title="Physics View">
    {sidebarOpen ? "◀" : "▶"}
  </button>
  <aside class="trike-sidebar" class:open={sidebarOpen}>
    <TrikeViz visible={sidebarOpen} />
  </aside>

</div>
