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
    } catch (error) {
      loadError = error instanceof Error ? error.message : String(error);
    }

    // Connect WebSocket AFTER initial REST data loads so WS frames don't get wiped
    stream = connectStream(ingestMessage, (connected) => wsConnected.set(connected));
  }

  async function refreshStatus() {
    try {
      const payload: BackendStatus = await getStatus();
      status.set(payload);
    } catch (error) {
      status.update((current) => ({
        ...current,
        backend_online: false,
        adapter_connected: false,
        esp32_connected: false,
        warning: error instanceof Error ? error.message : String(error)
      }));
    }
  }

  async function resetFrames() {
    if (!window.confirm("Clear all stored CAN frames? This cannot be undone.")) return;
    try {
      await clearFrames();
      frames.set([]);
      loadError = "";
    } catch (err) {
      loadError = "Reset failed: " + (err instanceof Error ? err.message : String(err));
    }
  }

  async function restartBridgeHandler() {
    if (!window.confirm("Restart the bridge connection? This will disconnect and reconnect the CAN transport.")) return;
    try {
      await restartBridge();
      loadError = "";
    } catch (err) {
      loadError = "Restart failed: " + (err instanceof Error ? err.message : String(err));
    }
  }

  async function stopBridgeHandler() {
    if (!window.confirm("Stop the bridge connection? The backend stays online but CAN traffic will stop.")) return;
    try {
      await stopBridge();
      loadError = "";
    } catch (err) {
      loadError = "Stop failed: " + (err instanceof Error ? err.message : String(err));
    }
  }
</script>

<div class="app-shell">
  <header class="topbar">
    <div>
      <p class="eyebrow">Dual CAN Bus</p>
      <h1>E-Trike Debug</h1>
    </div>
    <div class="status-strip">
      <span class:good={$status.backend_online} class="status-pill">Backend {$status.backend_online ? "Online" : "Offline"}</span>
      <span class:good={$status.bridge?.link_open || $status.serial?.port_open} class="status-pill">
        Link {($status.bridge?.link_open || $status.serial?.port_open) ? "Open" : "Closed"}
      </span>
      <span class:good={$status.adapter_connected || $status.esp32_connected} class="status-pill">
        {$status.bridge?.adapter ?? "Adapter"} {($status.adapter_connected || $status.esp32_connected) ? "Online" : "Offline"}
      </span>
      {#if $status.bus_detection?.bus}
        {@const bd = $status.bus_detection}
        <span class:good={bd.confidence === "high"} class="status-pill">
          Bus: {bd.bus.toUpperCase()}{bd.confidence === "high" ? " ✓" : bd.confidence === "low" ? " ?" : " …"}
        </span>
      {/if}
    </div>
    <div class="action-strip">
      <button type="button" class="action-btn" on:click={resetFrames} title="Clear all stored CAN frames">
        ⟳ Reset
      </button>
      <button type="button" class="action-btn" on:click={restartBridgeHandler} title="Restart the CAN bridge connection">
        ↻ Restart
      </button>
      <button type="button" class="action-btn danger" on:click={stopBridgeHandler} title="Stop the CAN bridge connection">
        ⏹ Stop
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
</div>
