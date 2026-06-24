<script lang="ts">
  import { onMount } from "svelte";
  import CanInjector from "./components/CanInjector.svelte";
  import CanMonitor from "./components/CanMonitor.svelte";
  import Dashboard from "./components/Dashboard.svelte";
  import Stats from "./components/Stats.svelte";
  import { getCanIds, getFrames, getStats, getStatus, getTemplates, type BackendStatus } from "./lib/api";
  import type { CanMessageDef, InjectionTemplate } from "./lib/can-decoder";
  import { connectStream, type StreamHandle } from "./lib/ws";
  import { ingestInitialFrames, ingestMessage, stats, status, wsConnected } from "./stores/can";

  type Tab = "dashboard" | "monitor" | "injector" | "stats";

  let activeTab: Tab = "dashboard";
  let ids: CanMessageDef[] = [];
  let templates: InjectionTemplate[] = [];
  let loadError = "";
  let stream: StreamHandle | null = null;

  const tabs: Array<{ id: Tab; label: string }> = [
    { id: "dashboard", label: "Dashboard" },
    { id: "monitor", label: "CAN Monitor" },
    { id: "injector", label: "Injector" },
    { id: "stats", label: "Statistics" }
  ];

  onMount(() => {
    void bootstrap();
    const timer = window.setInterval(refreshStatus, 3000);
    stream = connectStream(ingestMessage, (connected) => wsConnected.set(connected));

    return () => {
      window.clearInterval(timer);
      stream?.close();
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
      <CanMonitor {ids} {stream} />
    {:else if activeTab === "injector"}
      <CanInjector {ids} {templates} />
    {:else}
      <Stats {ids} />
    {/if}
  </main>
</div>
