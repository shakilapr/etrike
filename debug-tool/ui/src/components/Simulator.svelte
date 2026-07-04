<script lang="ts">
  import { startPeriodic, stopPeriodic } from "../lib/api";
  import { status } from "../stores/can";
  import { logError } from "../stores/errors";
  import { simMasterOn, simEnabled, simRunning, simToggle, simAddRunning, simRemoveRunning, simClearRunning } from "../stores/simulator";

  interface SimSignal { key: string; label: string; bus: "high"|"low"; id: string; hz: number; dlc: number; data: number[]; desc: string; }
  interface EcuGroup  { id: string; name: string; signals: SimSignal[]; }

  // ═══ ECU Groups — one per controller. Each group has a master toggle + individual signals. ═══
  const ECU_GROUPS: EcuGroup[] = [
    {
      id: "host", name: "HOST (Drive-by-Wire PC)",
      signals: [
        { key:"host_hb",     label:"Heartbeat",        bus:"high", id:"0x7FC", hz:2,   dlc:2, data:[1,0],       desc:"Alive ctr=1, health OK" },
        { key:"host_drive",  label:"Drive Command",    bus:"high", id:"0x300", hz:50,  dlc:8, data:[0,0,0,0,0,0,0,1], desc:"Speed 0, yaw 0, gear D" },
        { key:"host_brake",  label:"Brake Request",    bus:"high", id:"0x301", hz:10,  dlc:4, data:[0,0,0,0],   desc:"Brake 0 kPa (released)" },
        { key:"host_obst",   label:"Obstacle Distance",bus:"high", id:"0x400", hz:10,  dlc:4, data:[0xFF,0xFF,0xFF,0xFF], desc:"Clear (0xFFFFFFFF)" },
      ]
    },
    {
      id: "rt", name: "RT (Gateway Controller)",
      signals: [
        { key:"rt_hb",       label:"RT Heartbeat",     bus:"high", id:"0x7FD", hz:2,   dlc:2, data:[1,0],       desc:"Alive ctr=1, health OK" },
        { key:"rt_state",    label:"RT State Report",  bus:"high", id:"0x210", hz:10,  dlc:6, data:[0,0,0,0,0,0], desc:"MANUAL mode, Normal safety" },
        { key:"rt_throttle", label:"Throttle Status",  bus:"high", id:"0x120", hz:100, dlc:2, data:[0,0],       desc:"Speed 0 mm/s" },
        { key:"rt_motor_fwd",label:"Motor Feedback",   bus:"high", id:"0x206", hz:50,  dlc:4, data:[0,0,0,0],   desc:"Speed 0, gear N, no faults" },
      ]
    },
    {
      id: "sys", name: "SYS (Safety/Body Controller)",
      signals: [
        { key:"sys_hb",      label:"SYS Heartbeat",    bus:"low",  id:"0x7FE", hz:10,  dlc:2, data:[1,0],       desc:"Alive ctr=1, health OK" },
        { key:"sys_safety",  label:"Safety Status",    bus:"low",  id:"0x011", hz:5,   dlc:3, data:[0,1,0],     desc:"ESTOP clear, HB OK, lights off" },
        { key:"sys_diag",    label:"Diagnostics",      bus:"low",  id:"0x600", hz:1,   dlc:8, data:[0,0,1,0,0,0,0,0], desc:"MANUAL, brake OK, HB OK" },
      ]
    },
    {
      id: "mtr", name: "MTR (Motor Controller)",
      signals: [
        { key:"mtr_fbk",     label:"Motor Feedback",   bus:"low",  id:"0x206", hz:50,  dlc:4, data:[0,0,0,0],   desc:"Speed 0, gear N, no faults" },
        { key:"mtr_throttle",label:"Throttle Status",  bus:"low",  id:"0x120", hz:100, dlc:2, data:[0,0],       desc:"Speed 0 mm/s" },
      ]
    },
    {
      id: "ses", name: "SES / EPS-C (Steering ECU)",
      signals: [
        { key:"ses_status",  label:"SES Status",       bus:"low",  id:"0x201", hz:100, dlc:8, data:[0x01,0,0,0,0,0,0x10,0xFF], desc:"Angle OK, 0°, aligned" },
        { key:"ses_errinfo", label:"SES Error Info",   bus:"low",  id:"0x202", hz:10,  dlc:8, data:[0,0,0,0,0,0,0,0], desc:"No faults (all-zero mask)" },
      ]
    },
    {
      id: "seb", name: "SEB (Brake-by-Wire ECU)",
      signals: [
        { key:"seb_status",  label:"SEB Status",       bus:"low",  id:"0x721", hz:100, dlc:8, data:[0x03,0,0x58,0x02,0,0,0x10,0xFF], desc:"Aligned, stroke 600, no faults" },
        { key:"seb_errinfo", label:"SEB Error Info",   bus:"low",  id:"0x731", hz:10,  dlc:8, data:[0,0,0,0,0,0,0,0], desc:"No faults (all-zero mask)" },
      ]
    },
  ];

  // Initialize all signals as enabled
  if ($simEnabled.size === 0) {
    const all = ECU_GROUPS.flatMap(g => g.signals.map(s => s.key));
    simEnabled.set(new Set(all));
  }

  let sending = false;
  const connected = () => $status.bridge?.connected ?? false;

  async function startOne(key: string) {
    const sig = ECU_GROUPS.flatMap(g => g.signals).find(s => s.key === key);
    if (!sig || $simRunning.has(key)) return;
    try { await startPeriodic({ bus:sig.bus, id:sig.id, dlc:sig.dlc, data:[...sig.data], interval_ms:Math.round(1000/sig.hz) }); simAddRunning(key); }
    catch(e) { logError("Sim "+sig.label+": "+(e instanceof Error?e.message:String(e))); }
  }

  async function stopOne(key: string) {
    const sig = ECU_GROUPS.flatMap(g => g.signals).find(s => s.key === key);
    if (!sig || !$simRunning.has(key)) return;
    try { await stopPeriodic(sig.bus, sig.id); simRemoveRunning(key); } catch {}
  }

  function groupKeys(group: EcuGroup): string[] { return group.signals.map(s => s.key); }
  function groupEnabled(group: EcuGroup): boolean { return group.signals.every(s => $simEnabled.has(s.key)); }
  function groupRunning(group: EcuGroup): boolean { return group.signals.some(s => $simRunning.has(s.key)); }

  async function toggleGroup(group: EcuGroup) {
    if (sending) return;
    const keys = groupKeys(group);
    const allOn = groupEnabled(group);
    sending = true;
    if (allOn) {
      for (const k of keys) { simToggle(k); await stopOne(k); }
      logError(group.name + " OFF");
    } else {
      for (const k of keys) { if (!$simEnabled.has(k)) simToggle(k); }
      if ($simMasterOn) { for (const k of keys) await startOne(k); }
      logError(group.name + " ON");
    }
    sending = false;
  }

  function toggleSignal(key: string, group: EcuGroup) {
    simToggle(key);
    if ($simMasterOn && $simRunning.has(key)) stopOne(key);
    else if ($simMasterOn && $simEnabled.has(key)) startOne(key);
  }

  async function toggleMaster() {
    if (sending) return;
    sending = true; const wasOn = $simMasterOn; simMasterOn.set(!wasOn);
    if (!wasOn) {
      for (const g of ECU_GROUPS) for (const s of g.signals) { if ($simEnabled.has(s.key)) await startOne(s.key); }
      logError("Simulator ON — " + $simRunning.size + " signals across " + ECU_GROUPS.filter(g => groupRunning(g)).length + " ECUs");
    } else {
      for (const k of [...$simRunning]) await stopOne(k); simClearRunning();
      logError("Simulator OFF");
    }
    sending = false;
  }
</script>

<div class="sim-panel">
  <div class="sim-header">
    <span class="sim-title">CAN Simulator</span>
    <button class="sim-master {$simMasterOn ? 'on' : 'off'}" disabled={sending || !connected()} on:click={toggleMaster}>
      {$simMasterOn ? "■ STOP" : "▶ START"}
    </button>
    {#if !connected()}<span class="sim-warn">bridge offline</span>{/if}
    <span class="sim-info">{$simMasterOn ? $simRunning.size + " signals live" : "idle"}</span>
  </div>

  <p class="sim-desc">
    Simulate missing ECUs so the system has CAN feedback during bench testing.
    Toggle entire ECU groups or individual signals. Works with mixed real+sim hardware.
  </p>

  {#each ECU_GROUPS as group}
    <div class="sim-group" class:active={groupRunning(group)}>
      <div class="sim-group-head">
        <button class="sim-group-toggle" disabled={sending || !connected()} on:click={() => toggleGroup(group)}>
          {groupEnabled(group) ? "◼" : "▶"} {group.name}
        </button>
        {#if groupRunning(group)}<span class="sim-dot on"></span>{/if}
      </div>
      <div class="sim-group-signals">
        {#each group.signals as sig}
          <label class="sim-card" class:active={$simRunning.has(sig.key)} class:disabled={!connected()}>
            <input type="checkbox" checked={$simEnabled.has(sig.key)} disabled={!connected()} on:change={() => toggleSignal(sig.key, group)} />
            <span class="sim-card-label">{sig.label}</span>
            <span class="sim-card-meta">{sig.bus} {sig.id} · {sig.hz}Hz</span>
            <span class="sim-card-desc">{sig.desc}</span>
            {#if $simRunning.has(sig.key)}<span class="sim-dot on"></span>{/if}
          </label>
        {/each}
      </div>
    </div>
  {/each}
</div>
