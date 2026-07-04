<script lang="ts">
  import { startPeriodic, stopPeriodic } from "../lib/api";
  import { status } from "../stores/can";
  import { ecuPresence } from "../stores/telemetry";
  import { logError, logInfo } from "../stores/errors";

  interface EmuSignal { key: string; bus: "high"|"low"; id: string; hz: number; dlc: number; data: number[]; }
  interface EmuEcu { id: string; name: string; desc: string; signals: EmuSignal[]; }

  // ═══ ECU definitions — essential frames each controller sends ═══
  const ECUS: EmuEcu[] = [
    {
      id: "host", name: "HOST", desc: "Drive-by-wire PC. Sends drive, brake, obstacle, heartbeat on high bus.",
      signals: [
        { key:"e_host_hb",    bus:"high", id:"0x7FC", hz:2,   dlc:2, data:[1,0] },
        { key:"e_host_drive", bus:"high", id:"0x300", hz:50,  dlc:8, data:[0,0,0,0,0,0,0,1] },
        { key:"e_host_brake", bus:"high", id:"0x301", hz:10,  dlc:4, data:[0,0,0,0] },
        { key:"e_host_obst",  bus:"high", id:"0x400", hz:10,  dlc:4, data:[0xFF,0xFF,0xFF,0xFF] },
      ]
    },
    {
      id: "rt", name: "RT", desc: "Gateway controller. Forwards drive/brake to low bus, reports state on high bus.",
      signals: [
        { key:"e_rt_hb",     bus:"high", id:"0x7FD", hz:2,   dlc:2, data:[1,0] },
        { key:"e_rt_state",  bus:"high", id:"0x210", hz:10,  dlc:6, data:[0,0,0,0,0,0] },
        { key:"e_rt_thr",    bus:"high", id:"0x120", hz:100, dlc:2, data:[0,0] },
        { key:"e_rt_motor",  bus:"high", id:"0x206", hz:50,  dlc:4, data:[0,0,0,0] },
      ]
    },
    {
      id: "sys", name: "SYS", desc: "Safety/body controller. Sends heartbeat, safety status, diagnostics on low bus.",
      signals: [
        { key:"e_sys_hb",    bus:"low",  id:"0x7FE", hz:10,  dlc:2, data:[1,0] },
        { key:"e_sys_safety",bus:"low",  id:"0x011", hz:5,   dlc:3, data:[0,1,0] },
        { key:"e_sys_diag",  bus:"low",  id:"0x600", hz:1,   dlc:8, data:[0,0,1,0,0,0,0,0] },
      ]
    },
    {
      id: "mtr", name: "MTR", desc: "Motor controller. Sends motor feedback and throttle status on low bus.",
      signals: [
        { key:"e_mtr_fbk",   bus:"low",  id:"0x206", hz:50,  dlc:4, data:[0,0,0,0] },
        { key:"e_mtr_thr",   bus:"low",  id:"0x120", hz:100, dlc:2, data:[0,0] },
      ]
    },
    {
      id: "ses", name: "SES", desc: "Steering ECU (EPS-C). Sends steering status and error info on low bus.",
      signals: [
        { key:"e_ses_status",bus:"low",  id:"0x201", hz:100, dlc:8, data:[0x01,0,0,0,0,0,0x10,0xFF] },
        { key:"e_ses_err",   bus:"low",  id:"0x202", hz:10,  dlc:8, data:[0,0,0,0,0,0,0,0] },
      ]
    },
    {
      id: "seb", name: "SEB", desc: "Brake-by-wire ECU. Sends brake status and error info on low bus.",
      signals: [
        { key:"e_seb_status",bus:"low",  id:"0x721", hz:100, dlc:8, data:[0x03,0,0x58,0x02,0,0,0x10,0xFF] },
        { key:"e_seb_err",   bus:"low",  id:"0x731", hz:10,  dlc:8, data:[0,0,0,0,0,0,0,0] },
      ]
    },
  ];

  // ── State ──
  let emulating = new Set<string>();   // ECU ids currently sending
  let sending = false;
  let activeEcu: string | null = null;
  const connected = () => $status.bridge?.connected ?? false;

  function ecuKeys(ecu: EmuEcu): string[] { return ecu.signals.map(s => s.key); }
  function ecuLive(ecu: EmuEcu): boolean { return ecu.signals.some(s => emulating.has(s.key)); }
  function ecuPresent(id: string): boolean {
    const p = $ecuPresence as Record<string,boolean>;
    return p[id] === true;
  }
  const missingEcus = () => ECUS.filter(e => !ecuPresent(e.id));

  async function startEcu(ecu: EmuEcu) {
    if (sending) return;
    sending = true; activeEcu = ecu.id;
    for (const sig of ecu.signals) {
      try {
        await startPeriodic({ bus:sig.bus, id:sig.id, dlc:sig.dlc, data:[...sig.data], interval_ms:Math.round(1000/sig.hz) });
        emulating.add(sig.key);
      } catch(e) { logError(ecu.name+": "+(e instanceof Error?e.message:String(e))); }
    }
    emulating = new Set(emulating);
    logInfo("Emulating " + ecu.name + " — " + ecu.signals.length + " signals");
    sending = false; activeEcu = null;
  }

  async function stopEcu(ecu: EmuEcu) {
    if (sending) return;
    sending = true; activeEcu = ecu.id;
    for (const sig of ecu.signals) {
      try { await stopPeriodic(sig.bus, sig.id); emulating.delete(sig.key); } catch {}
    }
    emulating = new Set(emulating);
    logInfo(ecu.name + " emulation stopped");
    sending = false; activeEcu = null;
  }

  async function emulateMissing() {
    if (sending) return;
    sending = true;
    const missing = missingEcus();
    if (missing.length === 0) { logInfo("All ECUs already present"); sending = false; return; }
    for (const ecu of missing) {
      for (const sig of ecu.signals) {
        try { await startPeriodic({ bus:sig.bus, id:sig.id, dlc:sig.dlc, data:[...sig.data], interval_ms:Math.round(1000/sig.hz) }); emulating.add(sig.key); } catch {}
      }
    }
    emulating = new Set(emulating);
    logInfo("Emulating " + missing.length + " missing ECUs: " + missing.map(e=>e.name).join(", "));
    sending = false;
  }

  async function stopAll() {
    if (sending) return;
    sending = true;
    for (const k of [...emulating]) {
      const ecu = ECUS.find(e => e.signals.some(s => s.key === k));
      const sig = ecu?.signals.find(s => s.key === k);
      if (sig) try { await stopPeriodic(sig.bus, sig.id); } catch {}
    }
    emulating.clear(); emulating = new Set(emulating);
    logInfo("All emulation stopped");
    sending = false;
  }
</script>

<div class="emu-panel">
  <div class="emu-header">
    <span class="emu-title">CAN Emulator</span>
    <span class="emu-sub">Injects CAN frames to replace missing ECUs on the physical bus.</span>
    <div class="emu-actions">
      <button class="emu-btn quick" disabled={sending || !connected() || missingEcus().length===0} on:click={emulateMissing}>
        ▶ Emulate missing ({missingEcus().length})
      </button>
      {#if emulating.size > 0}
        <button class="emu-btn stop" disabled={sending} on:click={stopAll}>■ Stop all</button>
      {/if}
    </div>
  </div>

  {#if !connected()}
    <div class="emu-warn">Bridge not connected. Connect CANalyzer or ESP32 to inject frames.</div>
  {/if}

  <div class="emu-grid">
    {#each ECUS as ecu}
      {@const live = ecuLive(ecu)}
      {@const present = ecuPresent(ecu.id)}
      {@const busy = sending && activeEcu === ecu.id}
      <div class="emu-card" class:live class:present>
        <div class="emu-card-head">
          <span class="emu-card-name">{ecu.name}</span>
          {#if present}
            <span class="emu-badge ok">detected</span>
          {:else if live}
            <span class="emu-badge emu">emulated</span>
          {:else}
            <span class="emu-badge missing">missing</span>
          {/if}
        </div>
        <p class="emu-card-desc">{ecu.desc}</p>
        <div class="emu-card-sigs">
          {ecu.signals.length} signals: {ecu.signals.map(s => s.id).join(" ")} @ {ecu.signals.map(s => s.hz+"Hz").join("/")}
        </div>
        <button
          class="emu-card-btn"
          disabled={busy || sending || !connected()}
          on:click={() => live ? stopEcu(ecu) : startEcu(ecu)}
        >
          {busy ? "…" : live ? "■ Stop" : "▶ Emulate"}
        </button>
      </div>
    {/each}
  </div>
</div>
