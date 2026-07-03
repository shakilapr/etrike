<script lang="ts">
  import { startPeriodic, stopPeriodic } from "../lib/api";
  import { status } from "../stores/can";
  import { logError } from "../stores/errors";
  import { simMasterOn, simEnabled, simRunning, simToggle, simAddRunning, simRemoveRunning, simClearRunning } from "../stores/simulator";

  interface SimSignal {
    key: string;
    label: string;
    bus: "high" | "low";
    id: string;
    hz: number;
    dlc: number;
    data: number[];
  }

  const SIGNALS: SimSignal[] = [
    { key: "seb_status",  label: "SEB Status",     bus: "low", id: "0x721", hz: 100, dlc: 8, data: [0x03, 0, 0x58, 0x02, 0, 0, 0x10, 0xFF] },
    { key: "seb_errinfo", label: "SEB Errors",      bus: "low", id: "0x731", hz: 10,  dlc: 8, data: [0, 0, 0, 0, 0, 0, 0, 0] },
    { key: "ses_status",  label: "SES Status",      bus: "low", id: "0x201", hz: 100, dlc: 8, data: [0x01, 0, 0, 0, 0, 0, 0x10, 0xFF] },
    { key: "ses_errinfo", label: "SES Errors",       bus: "low", id: "0x202", hz: 10,  dlc: 8, data: [0, 0, 0, 0, 0, 0, 0, 0] },
    { key: "motor_fbk",   label: "Motor Feedback",  bus: "low", id: "0x206", hz: 50,  dlc: 4, data: [0, 0, 0, 0] },
    { key: "sys_hb",      label: "SYS Heartbeat",   bus: "low", id: "0x7FE", hz: 10,  dlc: 2, data: [1, 0] },
  ];

  if ($simEnabled.size === 0) simEnabled.set(new Set(SIGNALS.map((s) => s.key)));

  let sending = false;
  const connected = () => $status.bridge?.connected ?? false;

  async function startOne(key: string) {
    const sig = SIGNALS.find((s) => s.key === key);
    if (!sig || $simRunning.has(key)) return;
    try {
      await startPeriodic({ bus: sig.bus, id: sig.id, dlc: sig.dlc, data: [...sig.data], interval_ms: Math.round(1000 / sig.hz) });
      simAddRunning(key);
    } catch (e) {
      logError("Sim " + sig.label + ": " + (e instanceof Error ? e.message : String(e)));
    }
  }

  async function stopOne(key: string) {
    const sig = SIGNALS.find((s) => s.key === key);
    if (!sig || !$simRunning.has(key)) return;
    try { await stopPeriodic(sig.bus, sig.id); simRemoveRunning(key); } catch {}
  }

  async function toggleMaster() {
    if (sending) return;
    sending = true;
    const wasOn = $simMasterOn;
    simMasterOn.set(!wasOn);
    if (!wasOn) {
      for (const key of [...$simEnabled]) await startOne(key);
      logError("Sim ON — " + $simRunning.size + " signals");
    } else {
      for (const key of [...$simRunning]) await stopOne(key);
      simClearRunning();
      logError("Sim OFF");
    }
    sending = false;
  }

  function toggleSignal(key: string) {
    simToggle(key);
    if ($simMasterOn && $simRunning.has(key)) stopOne(key);
    else if ($simMasterOn && $simEnabled.has(key)) startOne(key);
  }
</script>

<div class="sim-panel">
  <div class="sim-header">
    <span class="sim-title">CAN Simulator</span>
    <button
      class="sim-master {$simMasterOn ? 'on' : 'off'}"
      disabled={sending || !connected()}
      on:click={toggleMaster}
    >
      {$simMasterOn ? "■ STOP" : "▶ START"}
    </button>
    {#if !connected()}
      <span class="sim-warn">bridge offline</span>
    {/if}
  </div>

  <p class="sim-desc">Bench-test substitute for missing ECUs. Sends healthy CAN frames so the system sees live hardware.</p>

  <div class="sim-grid">
    {#each SIGNALS as sig}
      <label class="sim-card" class:active={$simRunning.has(sig.key)} class:disabled={!connected()}>
        <input
          type="checkbox"
          checked={$simEnabled.has(sig.key)}
          disabled={!connected()}
          on:change={() => toggleSignal(sig.key)}
        />
        <span class="sim-card-label">{sig.label}</span>
        <span class="sim-card-meta">{sig.id} · {sig.hz}Hz</span>
        {#if $simRunning.has(sig.key)}
          <span class="sim-dot on"></span>
        {:else if $simEnabled.has(sig.key) && $simMasterOn}
          <span class="sim-dot pending"></span>
        {/if}
      </label>
    {/each}
  </div>
</div>
