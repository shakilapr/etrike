<script lang="ts">
  import { startPeriodic, stopPeriodic } from "../lib/api";
  import { status } from "../stores/can";
  import { logError } from "../stores/errors";

  // ── Signal definitions ──
  // Each entry: label, CAN bus, ID, DLC, rate in Hz, data bytes
  interface SimSignal {
    key: string;
    section: string;
    label: string;
    bus: "high" | "low";
    id: string;
    hz: number;
    dlc: number;
    data: number[];
    description: string;
  }

  const SIGNALS: SimSignal[] = [
    // ── Brake (SEB) ──
    {
      key: "seb_status", section: "Brake (SEB)", label: "SEB Status",
      bus: "low", id: "0x721", hz: 100, dlc: 8,
      data: [0x03, 0, 0x58, 0x02, 0, 0, 0x10, 0xFF],
      description: "Alignment OK, stroke 600, no fault, rolling ctr=1"
    },
    {
      key: "seb_errinfo", section: "Brake (SEB)", label: "SEB Error Info",
      bus: "low", id: "0x731", hz: 10, dlc: 8,
      data: [0, 0, 0, 0, 0, 0, 0, 0],
      description: "No faults (all-zero fault mask)"
    },
    {
      key: "seb_version", section: "Brake (SEB)", label: "SEB Version",
      bus: "low", id: "0x741", hz: 1, dlc: 8,
      data: [1, 1, 0, 0, 0, 0, 0, 0],
      description: "SW v1, HW v1"
    },
    // ── Steering (EPS-C) ──
    {
      key: "ses_status", section: "Steering (EPS-C)", label: "SES Status",
      bus: "low", id: "0x201", hz: 100, dlc: 8,
      data: [0x01, 0, 0, 0, 0, 0, 0x10, 0xFF],
      description: "Angle OK, 0°, rolling ctr=1"
    },
    {
      key: "ses_errinfo", section: "Steering (EPS-C)", label: "SES Error Info",
      bus: "low", id: "0x202", hz: 10, dlc: 8,
      data: [0, 0, 0, 0, 0, 0, 0, 0],
      description: "No faults (all-zero fault mask)"
    },
    {
      key: "ses_version", section: "Steering (EPS-C)", label: "SES Version",
      bus: "low", id: "0x203", hz: 1, dlc: 8,
      data: [1, 1, 0, 0, 0, 0, 0, 0],
      description: "SW v1, HW v1"
    },
    // ── Motor (MTR) ──
    {
      key: "motor_fbk", section: "Motor (MTR)", label: "Motor Feedback",
      bus: "low", id: "0x206", hz: 50, dlc: 4,
      data: [0, 0, 0, 0],
      description: "Speed 0 mm/s, gear N, no faults"
    },
    // ── System ──
    {
      key: "sys_hb", section: "System (SYS)", label: "SYS Heartbeat",
      bus: "low", id: "0x7FE", hz: 10, dlc: 2,
      data: [1, 0],
      description: "Alive counter=1, health OK"
    },
  ];

  // Group signals by section
  const sections = [...new Set(SIGNALS.map((s) => s.section))];

  // ── State ──
  let masterOn = false;
  let enabled = writableSet(SIGNALS.map((s) => s.key)); // all on by default
  let running = new Set<string>();           // actually sending right now
  let expanded = writableSet(sections);      // all expanded by default
  let sending = false;

  function writableSet(initial: string[]) {
    let s = new Set(initial);
    return {
      has(k: string) { return s.has(k); },
      toggle(k: string) { if (s.has(k)) s.delete(k); else s.add(k); s = s; return s; },
      all() { return s; },
    };
  }

  function toggleSignal(key: string) {
    enabled.toggle(key);
    if (masterOn && running.has(key)) {
      stopOne(key);
    } else if (masterOn && enabled.has(key)) {
      startOne(key);
    }
  }

  async function startOne(key: string) {
    const sig = SIGNALS.find((s) => s.key === key);
    if (!sig || running.has(key)) return;
    try {
      await startPeriodic({
        bus: sig.bus, id: sig.id, dlc: sig.dlc, data: [...sig.data],
        interval_ms: Math.round(1000 / sig.hz),
      });
      running.add(key);
    } catch (e) {
      logError("Sim start " + sig.label + ": " + (e instanceof Error ? e.message : String(e)));
    }
  }

  async function stopOne(key: string) {
    const sig = SIGNALS.find((s) => s.key === key);
    if (!sig || !running.has(key)) return;
    try {
      await stopPeriodic(sig.bus, sig.id);
      running.delete(key);
    } catch (e) {
      logError("Sim stop " + sig.label + ": " + (e instanceof Error ? e.message : String(e)));
    }
  }

  async function toggleMaster() {
    if (sending) return;
    sending = true;
    masterOn = !masterOn;
    const keys = [...enabled.all()];
    if (masterOn) {
      for (const key of keys) await startOne(key);
      logError("Simulator ON — " + running.size + " signals running");
    } else {
      for (const key of [...running]) await stopOne(key);
      logError("Simulator OFF");
    }
    sending = false;
  }

  const connected = () => $status.bridge?.connected ?? false;
</script>

<div class="sim-panel">
  <div class="sim-header">
    <span class="sim-title">CAN Bus Simulator</span>
    <button
      type="button"
      class="sim-master {masterOn ? 'on' : 'off'}"
      disabled={sending || !connected()}
      on:click={toggleMaster}
    >
      {masterOn ? "◼ STOP" : "▶ START"}
    </button>
    {#if !connected()}
      <span class="sim-warn">Bridge not connected</span>
    {/if}
  </div>

  <p class="sim-desc">
    Simulates missing ECUs so the system has CAN feedback during bench testing.
    Only ESP32 and CAN receivers are connected — no real vehicle hardware.
  </p>

  {#each sections as section}
    <details open={expanded.has(section)}>
      <summary on:click|preventDefault={() => expanded.toggle(section)}>
        <span class="section-name">{section}</span>
        <span class="section-arrow">{expanded.has(section) ? "▾" : "▸"}</span>
      </summary>
      <div class="signal-list">
        {#each SIGNALS.filter((s) => s.section === section) as sig}
          <label class="signal-row" class:disabled={!connected()}>
            <span class="sig-toggle">
              <input
                type="checkbox"
                checked={enabled.has(sig.key)}
                disabled={!connected()}
                on:change={() => toggleSignal(sig.key)}
              />
            </span>
            <span class="sig-info">
              <span class="sig-name">{sig.label}</span>
              <span class="sig-meta">{sig.bus} {sig.id} · {sig.hz} Hz · DLC={sig.dlc}</span>
            </span>
            <span class="sig-desc">{sig.description}</span>
            {#if running.has(sig.key)}
              <span class="sig-badge on">live</span>
            {:else if enabled.has(sig.key) && masterOn}
              <span class="sig-badge pending">...</span>
            {/if}
          </label>
        {/each}
      </div>
    </details>
  {/each}
</div>
