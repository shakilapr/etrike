<script lang="ts">
  import { onDestroy } from "svelte";
  import { sendFrame, simPeriodicStart, simPeriodicStop } from "../lib/api";
  import { status } from "../stores/can";
  import { ecuPresence } from "../stores/telemetry";
  import type { EcuPresence } from "../stores/telemetry";
  import { logError, logInfo } from "../stores/errors";
  import { softwareSimEnabled } from "../stores/emulator";
  import EcuTopology from "./EcuTopology.svelte";

  // ═══ Mode toggle: Physical (CAN hardware) vs Simulated (software loopback) ═══

  // ═══ Signal definition with dynamic data generator ═══
  interface EmuSignal {
    key: string; label: string; bus: "high"|"low"; id: string; hz: number; dlc: number;
    data(): number[];
    summary(data: number[]): string;
  }

  // ── Dynamic state (shared across signals) ──
  const counters: Record<string, number> = {};
  function counter(key: string, max = 255): number {
    if (!(key in counters)) counters[key] = 1;
    const v = counters[key];
    counters[key] = (v + 1) > max ? 1 : v + 1;
    return v;
  }
  function resetCounter(key: string) { counters[key] = 0; }

  // Behavioral state — updated by incoming frames or by signal generators
  let simSpeed = 0;       // mm/s — responds to drive commands
  let simGear = 0;        // 0=N, 1=D
  let simBrakeKpa = 0;    // kPa
  let simSteerAngle = 0;  // 0.1 deg units
  let simEstopActive = false;
  let simVehicleMode = 0;        // 0=MANUAL, 1=AUTO

  // ── ECU definitions ──
  interface EmuEcu { id: string; name: string; signals: EmuSignal[]; }
  // ── Data helpers ──
  function i32be(v: number): number[] { return [(v>>24)&0xFF,(v>>16)&0xFF,(v>>8)&0xFF,v&0xFF]; }
  function i16be(v: number): number[] { return [(v>>8)&0xFF,v&0xFF]; }
  function i16le(v: number): number[] { return [v&0xFF,(v>>8)&0xFF]; }

  const ECUS: EmuEcu[] = [
    {
      id: "host", name: "HOST (Drive-by-Wire)",
      signals: [
        { key:"e_host_hb",    label:"Heartbeat",        bus:"high", id:"0x7FC", hz:2,  dlc:2,
          data:()=>[counter("host_hb"),0], summary:(d)=>`alive=${d[0]}` },
        { key:"e_host_drive", label:"Drive Command",     bus:"high", id:"0x300", hz:50, dlc:8,
          data:()=>[...i32be(simSpeed),0,0,0,simGear], summary:()=>`speed=${simSpeed}mm/s, yaw=0, gear=${["N","D","S","R"][simGear]??"?"}` },
        { key:"e_host_brake", label:"Brake Request",     bus:"high", id:"0x301", hz:10, dlc:4,
          data:()=>i32be(simBrakeKpa), summary:()=>`${simBrakeKpa} kPa` },
        { key:"e_host_obst",  label:"Obstacle Distance", bus:"high", id:"0x400", hz:10, dlc:4,
          data:()=>[0xFF,0xFF,0xFF,0xFF], summary:()=>`clear` },
      ]
    },
    {
      id: "rt", name: "RT (Gateway)",
      signals: [
        { key:"e_rt_hb",     label:"RT Heartbeat",    bus:"high", id:"0x7FD", hz:2,   dlc:2,
          data:()=>[counter("rt_hb"),0], summary:(d)=>`alive=${d[0]}` },
        { key:"e_rt_state",  label:"State Report",    bus:"high", id:"0x210", hz:10,  dlc:6,
          data:()=>[simVehicleMode,simEstopActive?1:0,0,0,15,5], summary:()=>`${["MANUAL","AUTO","ESTOP"][simVehicleMode]}, ${simEstopActive?"InternalEstop":"Normal"}` },
        { key:"e_rt_thr",    label:"Throttle Status", bus:"high", id:"0x120", hz:100, dlc:2,
          data:()=>i16be(simSpeed), summary:()=>`${simSpeed} mm/s` },
        { key:"e_rt_motor",  label:"Motor Feedback",  bus:"high", id:"0x206", hz:50,  dlc:4,
          data:()=>[...i16be(simSpeed),simGear,0], summary:()=>`${simSpeed} mm/s, gear ${["N","D","S","R"][simGear]??"?"}` },
      ]
    },
    {
      id: "sys", name: "SYS (Safety/Body)",
      signals: [
        { key:"e_sys_hb",     label:"SYS Heartbeat",  bus:"low", id:"0x7FE", hz:10, dlc:2,
          data:()=>[counter("sys_hb"),0], summary:(d)=>`alive=${d[0]}` },
        { key:"e_sys_safety", label:"Safety Status",  bus:"low", id:"0x011", hz:5,  dlc:3,
          data:()=>[simEstopActive?1:0,1,0], summary:()=>`estop=${simEstopActive?1:0}, hb_ok=1` },
        { key:"e_sys_diag",   label:"Diagnostics",    bus:"low", id:"0x600", hz:1,  dlc:8,
          data:()=>[simVehicleMode,0,1,0,0,0,0,0], summary:()=>`${["MANUAL","AUTO","ESTOP"][simVehicleMode]}, brake off` },
      ]
    },
    {
      id: "mtr", name: "MTR (Motor)",
      signals: [
        { key:"e_mtr_fbk",  label:"Motor Feedback",  bus:"low", id:"0x206", hz:50,  dlc:4,
          data:()=>[...i16be(simSpeed),simGear,0], summary:()=>`${simSpeed} mm/s, gear ${["N","D","S","R"][simGear]??"?"}` },
        { key:"e_mtr_thr",  label:"Throttle Status", bus:"low", id:"0x120", hz:100, dlc:2,
          data:()=>i16be(simSpeed), summary:()=>`${simSpeed} mm/s` },
      ]
    },
    {
      id: "ses", name: "SES (Steering EPS-C)",
      signals: [
        { key:"e_ses_status", label:"SES Status",  bus:"low", id:"0x201", hz:100, dlc:8,
          data:()=>[0x01,0,...i16le(simSteerAngle),0,0,0,(counter("ses_roll")<<4)|0x01,0xFF],
          summary:()=>`angle=${(simSteerAngle/10).toFixed(1)}°, roll=${counters["ses_roll"]??0}` },
        { key:"e_ses_err",    label:"SES Errors",  bus:"low", id:"0x202", hz:10,  dlc:8,
          data:()=>[0,0,0,0,0,0,0,0], summary:()=>`no faults` },
      ]
    },
    {
      id: "seb", name: "SEB (Brake-by-Wire)",
      signals: [
        { key:"e_seb_status", label:"SEB Status",  bus:"low", id:"0x721", hz:100, dlc:8,
          data:()=>[0x03,0,...i16le(600),0,0,0,(counter("seb_roll")<<4)|0x01,0xFF],
          summary:()=>`stroke=600, roll=${counters["seb_roll"]??0}` },
        { key:"e_seb_err",    label:"SEB Errors",  bus:"low", id:"0x731", hz:10,  dlc:8,
          data:()=>[0,0,0,0,0,0,0,0], summary:()=>`no faults` },
      ]
    },
  ];

  // ── Frame ingestion — respond to incoming CAN commands ──
  import { frames } from "../stores/can";
  let lastIngestTs = 0;
  const unsubscribeFrames = frames.subscribe(($frames) => {
    if ($frames.length === 0) return;
    const latest = $frames[$frames.length - 1];
    if (latest.ts <= lastIngestTs) return;
    lastIngestTs = latest.ts;

    // Respond to drive commands: update simulated speed
    if (latest.id === "0x300" && latest.decoded) {
      const d = latest.decoded as Record<string, unknown>;
      const targetSpeed = (d.speed_mmps as number) ?? 0;
      const gear = (d.gear as number) ?? 0;
      // Smooth approach to target speed
      simSpeed = simSpeed + (targetSpeed - simSpeed) * 0.3;
      simGear = gear;
      if (targetSpeed !== 0) simBrakeKpa = 0;
    }
    // Respond to brake commands
    if (latest.id === "0x301" && latest.decoded) {
      const d = latest.decoded as Record<string, unknown>;
      simBrakeKpa = (d.brake_pressure_kpa as number) ?? 0;
      if (simBrakeKpa > 0) simSpeed = simSpeed * 0.5; // brake halves speed
    }
    // Respond to steering commands
    if (latest.id === "0x169" && latest.decoded) {
      const d = latest.decoded as Record<string, unknown>;
      const targetAngle = (d.target_angle as number) ?? 0;
      simSteerAngle = simSteerAngle + (targetAngle - simSteerAngle) * 0.5;
    }
    // ESTOP
    if (latest.id === "0x001") {
      simEstopActive = true;
      simSpeed = 0;
      simBrakeKpa = 5000;
    }
    // Mode changes
    if (latest.id === "0x110" && latest.decoded) {
      const d = latest.decoded as Record<string, unknown>;
      const newMode = (d.mode as number) ?? 0;
      if (newMode <= 1) simVehicleMode = newMode;
      if (newMode === 2) simEstopActive = true;
      if (simVehicleMode !== 2) simEstopActive = false;
    }
  });

  // ── Interval-based sending (dynamic data) ──
  let timers: Record<string, ReturnType<typeof setInterval>> = {};
  let running = new Set<string>();
  let sending = false;
  const connected = () => $status.bridge?.connected ?? false;
  const canSend = () => $softwareSimEnabled || connected();

  function hex(data: number[]): string {
    return data.map(b=>b.toString(16).toUpperCase().padStart(2,'0')).join(' ');
  }

  function ecuLive(ecu: EmuEcu): boolean { return ecu.signals.some(s=>running.has(s.key)); }
  function isEcuId(id: string): id is keyof EcuPresence {
    return id === "rt" || id === "sys" || id === "mtr" || id === "ses" || id === "seb";
  }
  function ecuPresent(id: string): boolean { return isEcuId(id) && $ecuPresence[id] === true; }

  async function startEcu(ecu: EmuEcu) {
    if (sending) return;
    sending = true;
    for (const sig of ecu.signals) {
      if (running.has(sig.key)) continue;
      resetCounter(sig.key.replace("e_","").replace("host_","").replace("rt_","").replace("sys_","").replace("mtr_","").replace("ses_","").replace("seb_",""));
      const ms = Math.round(1000 / sig.hz);
      async function tick() {
        const data = sig.data();
        try {
          if ($softwareSimEnabled) {
            await simPeriodicStart({ bus:sig.bus, id:sig.id, dlc:sig.dlc, data, interval_ms: ms });
          } else {
            await sendFrame({ bus:sig.bus, id:sig.id, dlc:sig.dlc, data });
          }
        } catch {}
      }
      if ($softwareSimEnabled) {
        // Simulated mode: backend handles the interval
        await tick(); // first frame
        running.add(sig.key);
      } else {
        // Physical mode: client-side interval
        tick();
        timers[sig.key] = setInterval(tick, ms);
        running.add(sig.key);
      }
    }
    running = new Set(running);
    logInfo(ecu.name + " emulated (" + ($softwareSimEnabled ? "sim" : "physical") + ") — " + ecu.signals.length + " signals");
    sending = false;
  }

  async function stopEcu(ecu: EmuEcu) {
    if (sending) return;
    sending = true;
    for (const sig of ecu.signals) {
      if (timers[sig.key]) { clearInterval(timers[sig.key]); delete timers[sig.key]; }
      if ($softwareSimEnabled) {
        try { await simPeriodicStop(sig.bus, sig.id); } catch {}
      }
      running.delete(sig.key);
    }
    running = new Set(running);
    logInfo(ecu.name + " stopped");
    sending = false;
  }

  const missingEcus = () => ECUS.filter(e => !ecuPresent(e.id));

  async function emulateMissing() {
    if (sending) return;
    const missing = missingEcus();
    if (missing.length === 0) { logInfo("All ECUs already present"); return; }
    sending = true;
    for (const ecu of missing) await startEcu(ecu);
    logInfo("Emulating " + missing.length + " missing ECUs");
    sending = false;
  }

  async function stopAll() {
    if (sending) return;
    sending = true;
    for (const k of [...running]) {
      if (timers[k]) { clearInterval(timers[k]); delete timers[k]; }
    }
    running.clear(); running = new Set(running);
    logInfo("All emulation stopped");
    sending = false;
  }

  // Show live data for a signal
  function liveData(sig: EmuSignal): string {
    if (!running.has(sig.key)) return "";
    return hex(sig.data());
  }

  onDestroy(() => {
    unsubscribeFrames();
    for (const key of running) {
      const sig = ECUS.flatMap((ecu) => ecu.signals).find((item) => item.key === key);
      if (sig && $softwareSimEnabled) void simPeriodicStop(sig.bus, sig.id);
    }
    for (const timer of Object.values(timers)) clearInterval(timer);
    timers = {};
    running.clear();
  });
</script>

<div class="emu-panel">
  <EcuTopology />
  <div class="emu-header">
    <span class="emu-title">CAN Emulator</span>
    <!-- Mode toggle: Physical (CAN hardware) vs Simulated (software loopback) -->
    <label class="emu-mode-toggle" title="Simulated: no CAN hardware needed. Physical: requires CANalyzer/ESP32.">
      <input type="checkbox" bind:checked={$softwareSimEnabled} />
      <span class="emu-mode-label">{$softwareSimEnabled ? "Simulated" : "Physical"}</span>
    </label>
    <span class="emu-sub">{$softwareSimEnabled ? "Software loopback — no CAN hardware needed." : "CAN injection via bridge — needs CANalyzer/ESP32."}</span>
    <div class="emu-actions">
      <button class="emu-btn quick" disabled={sending || (!$softwareSimEnabled && !connected()) || missingEcus().length===0} on:click={emulateMissing}>
        ▶ Emulate missing ({missingEcus().length})
      </button>
      {#if running.size > 0}
        <button class="emu-btn stop" disabled={sending} on:click={stopAll}>■ Stop all ({running.size})</button>
      {/if}
    </div>
  </div>

  {#if !$softwareSimEnabled && !connected()}
    <div class="emu-warn">Bridge not connected. Switch to <strong>Simulated</strong> mode or connect CANalyzer/ESP32.</div>
  {/if}

  <div class="emu-grid">
    {#each ECUS as ecu}
      {@const live = ecuLive(ecu)}
      {@const present = ecuPresent(ecu.id)}
      <div class="emu-card" class:live class:present>
        <div class="emu-card-head">
          <span class="emu-card-name">{ecu.name}</span>
          <span class="emu-badge {present ? 'ok' : live ? 'emu' : 'missing'}">{present ? 'detected' : live ? 'emulated' : 'missing'}</span>
        </div>

        <!-- Signal table — fixed window showing what each signal sends -->
        <table class="emu-sig-table">
          <thead>
            <tr><th>Signal</th><th>ID</th><th>Hz</th><th>Data</th><th>Summary</th></tr>
          </thead>
          <tbody>
            {#each ecu.signals as sig}
              <tr class:active={running.has(sig.key)}>
                <td class="emu-sig-name">{sig.label}</td>
                <td class="emu-sig-id">{sig.bus[0].toUpperCase()}:{sig.id}</td>
                <td class="emu-sig-rate">{sig.hz}</td>
                <td class="emu-sig-data">{running.has(sig.key) ? liveData(sig) : hex(sig.data())}</td>
                <td class="emu-sig-summary">{running.has(sig.key) ? sig.summary(sig.data()) : sig.summary(sig.data())}</td>
              </tr>
            {/each}
          </tbody>
        </table>

        <button
          class="emu-card-btn"
          disabled={sending || !canSend()}
          on:click={() => live ? stopEcu(ecu) : startEcu(ecu)}
        >
          {live ? "■ Stop" : "▶ Emulate"}
        </button>
      </div>
    {/each}
  </div>
</div>
