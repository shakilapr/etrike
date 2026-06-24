<script lang="ts">
  import { onMount } from "svelte";
  import CanInjector from "./components/CanInjector.svelte";
  import CanMonitor from "./components/CanMonitor.svelte";
  import Dashboard from "./components/Dashboard.svelte";
  import PipelineView from "./components/PipelineView.svelte";
  import Stats from "./components/Stats.svelte";
  import { getCanIds, getFrames, getStats, getStatus, getTemplates, type BackendStatus } from "./lib/api";
  import type { Bus, CanMessageDef, InjectionTemplate } from "./lib/can-decoder";
  import { connectStream, type StreamHandle } from "./lib/ws";
  import { ingestInitialFrames, ingestMessage, stats, status, wsConnected } from "./stores/can";
  import { kbBus, kbEvent, type KbAction } from "./stores/keyboard";

  type Tab = "dashboard" | "monitor" | "injector" | "pipeline" | "stats";

  let activeTab: Tab = "dashboard";
  let ids: CanMessageDef[] = [];
  let templates: InjectionTemplate[] = [];
  let loadError = "";
  let stream: StreamHandle | null = null;

  const tabs: Array<{ id: Tab; label: string }> = [
    { id: "dashboard", label: "Dashboard" },
    { id: "monitor", label: "CAN Monitor" },
    { id: "injector", label: "Injector" },
    { id: "pipeline", label: "Pipeline" },
    { id: "stats", label: "Statistics" }
  ];

  // ── Keyboard controls ──
  let lastSpaceTs = 0;

  function handleKeydown(e: KeyboardEvent) {
    // Ignore when typing in inputs
    if (e.target instanceof HTMLInputElement || e.target instanceof HTMLSelectElement || e.target instanceof HTMLTextAreaElement) return;

    let action: KbAction | null = null;
    let bus: Bus | null = null;

    switch (e.key) {
      case "w": case "W": action = { type: "speed_up" }; break;
      case "s": case "S": action = { type: "speed_down" }; break;
      case "a": case "A": action = { type: "yaw_left" }; break;
      case "d": case "D": action = { type: "yaw_right" }; break;
      case "b": case "B": action = { type: "brake_set" }; break;
      case "r": case "R": action = { type: "brake_release" }; break;
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
    stream = connectStream(ingestMessage, (connected) => wsConnected.set(connected));
    window.addEventListener("keydown", handleKeydown);

    return () => {
      window.clearInterval(timer);
      stream?.close();
      window.removeEventListener("keydown", handleKeydown);
    };
  });

  async function bootstrap() {
    try {
      const [statusPayload, idsPayload, framesPayload, statsPayload, templatesPayload] = await Promise.all([
        getStatus(),
        getCanIds(),
        getFrames(),
        getStats(),
        getTemplates()
      ]);
      status.set(statusPayload);
      ids = idsPayload;
      ingestInitialFrames(framesPayload);
      stats.set(statsPayload);
      templates = templatesPayload;
      loadError = "";
    } catch (error) {
      loadError = error instanceof Error ? error.message : String(error);
    }
  }

  async function refreshStatus() {
    try {
      const payload: BackendStatus = await getStatus();
      status.set(payload);
    } catch (error) {
      status.update((current) => ({
        ...current,
        backend_online: false,
        warning: error instanceof Error ? error.message : String(error)
      }));
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
      <span class:good={$status.serial?.port_open} class="status-pill">Serial {$status.serial?.port_open ? "Open" : "Closed"}</span>
      <span class:good={$status.esp32_connected} class="status-pill">ESP32 {$status.esp32_connected ? "Online" : "Offline"}</span>
      {#if $status.bus_detection}
        {@const bd = $status.bus_detection}
        <span class:good={bd.confidence === "high"} class="status-pill">
          Bus: {bd.bus.toUpperCase()}{bd.confidence === "high" ? " ✓" : bd.confidence === "low" ? " ?" : " …"}
        </span>
      {/if}
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
    {:else if activeTab === "pipeline"}
      <PipelineView />
    {:else}
      <Stats {ids} />
    {/if}
  </main>
</div>
