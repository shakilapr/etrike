<script lang="ts">
  import { onMount } from "svelte";
  import { setMode, getModeDefaults } from "../lib/api";
  import type { WorkModeConfig } from "../lib/api";
  import { workMode, workModeReady, modeLabel } from "../stores/work-mode";
  import { logError, logInfo } from "../stores/errors";
  import EcuTopology from "./EcuTopology.svelte";

  const MODES: WorkModeConfig["mode"][] = ["full-sim", "emulator", "hybrid", "bench", "monitor"];

  const ECU_IDS: Array<{ id: string; label: string; description: string }> = [
    { id: "host", label: "HOST",  description: "Drive-by-Wire controller (speed, yaw, gear)" },
    { id: "rt",   label: "RT",   description: "Gateway / safety arbiter" },
    { id: "sys",  label: "SYS",  description: "Safety & body controller" },
    { id: "mtr",  label: "MTR",  description: "Motor controller" },
    { id: "ses",  label: "SES",  description: "Steering EPS-C (SES)" },
    { id: "seb",  label: "SEB",  description: "Brake-by-Wire (SEB)" },
  ];

  const BYPASS_LABELS: Array<{ key: keyof WorkModeConfig["bypasses"]; label: string; description: string }> = [
    { key: "sesSync",   label: "SES Sync",    description: "Bypass SES rolling counter & checksum validation" },
    { key: "sebSync",   label: "SEB Sync",    description: "Bypass SEB rolling counter & checksum validation" },
    { key: "mtrAbsent", label: "MTR Absent",  description: "Treat motor controller as intentionally missing" },
    { key: "benchSolo", label: "Bench Solo",  description: "Single-ECU bench mode — suppress missing-ECU faults" },
  ];

  let applying = false;
  let modeDefaults: Record<string, WorkModeConfig> = {};

  onMount(async () => {
    try {
      modeDefaults = await getModeDefaults();
    } catch {
      // Defaults will be empty — user can still manually configure
    }
  });

  // Local editable copy that the user manipulates before applying
  let draft: WorkModeConfig = { ...$workMode, bypasses: { ...$workMode.bypasses } };

  // Keep draft in sync when store updates externally (e.g., Topbar sets it)
  $: {
    draft = { ...$workMode, bypasses: { ...$workMode.bypasses } };
  }

  function selectPreset(mode: WorkModeConfig["mode"]) {
    const preset = modeDefaults[mode];
    if (preset) {
      draft = { ...preset, bypasses: { ...preset.bypasses } };
    } else {
      draft = { ...draft, mode };
    }
  }

  function toggleEcu(ecuId: string) {
    const current = draft.simulatedEcus;
    if (current.includes(ecuId)) {
      draft = { ...draft, simulatedEcus: current.filter((e) => e !== ecuId) };
    } else {
      draft = { ...draft, simulatedEcus: [...current, ecuId] };
    }
  }

  function toggleBypass(key: keyof WorkModeConfig["bypasses"]) {
    draft = { ...draft, bypasses: { ...draft.bypasses, [key]: !draft.bypasses[key] } };
  }

  async function applyConfig() {
    if (applying) return;
    applying = true;
    try {
      await setMode(draft);
      workMode.set(draft);
      logInfo(`Work mode set: ${modeLabel(draft.mode)}`);
    } catch (e) {
      logError("Mode apply failed: " + (e instanceof Error ? e.message : String(e)));
    } finally {
      applying = false;
    }
  }

  $: isDirty = JSON.stringify(draft) !== JSON.stringify($workMode);
  $: isFullSim = draft.mode === "full-sim";
</script>

<div class="wmc-panel">
  <EcuTopology />

  <div class="wmc-header">
    <span class="wmc-title">Work Mode Configurator</span>
    <span class="wmc-sub">Configure simulation mode and ECU participation</span>
  </div>

  <div class="wmc-body">

    <!-- ── Mode selector ── -->
    <section class="wmc-section">
      <h3 class="wmc-section-title">Mode Preset</h3>
      <div class="wmc-mode-grid">
        {#each MODES as m}
          <button
            class="wmc-mode-btn"
            class:active={draft.mode === m}
            on:click={() => selectPreset(m)}
            title={modeLabel(m)}
          >
            <span class="wmc-mode-name">{modeLabel(m)}</span>
          </button>
        {/each}
      </div>
    </section>

    <!-- ── Simulated ECUs (only relevant for full-sim / hybrid) ── -->
    <section class="wmc-section">
      <h3 class="wmc-section-title">
        Simulated ECUs
        <span class="wmc-badge">{draft.simulatedEcus.length} / {ECU_IDS.length}</span>
      </h3>
      <p class="wmc-hint">Which ECUs are replaced by software models in the backend engine.</p>
      <div class="wmc-ecu-grid">
        {#each ECU_IDS as ecu}
          {@const active = draft.simulatedEcus.includes(ecu.id)}
          <label class="wmc-ecu-row" title={ecu.description}>
            <input type="checkbox" checked={active} on:change={() => toggleEcu(ecu.id)} />
            <span class="wmc-ecu-label">{ecu.label}</span>
            <span class="wmc-ecu-desc">{ecu.description}</span>
          </label>
        {/each}
      </div>
    </section>

    <!-- ── Bypasses ── -->
    <section class="wmc-section">
      <h3 class="wmc-section-title">Bypasses</h3>
      <p class="wmc-hint">Protocol enforcement flags. Enable to suppress specific validations during bench testing.</p>
      <div class="wmc-bypass-grid">
        {#each BYPASS_LABELS as bp}
          {@const active = draft.bypasses[bp.key]}
          <label class="wmc-bypass-row" title={bp.description}>
            <input type="checkbox" checked={active} on:change={() => toggleBypass(bp.key)} />
            <span class="wmc-bypass-label">{bp.label}</span>
            <span class="wmc-bypass-desc">{bp.description}</span>
          </label>
        {/each}
      </div>
    </section>

    <!-- ── Inject to physical ── -->
    <section class="wmc-section">
      <h3 class="wmc-section-title">Physical Injection</h3>
      <label class="wmc-bypass-row" title="Forward emulated frames onto the physical CAN bus">
        <input type="checkbox" bind:checked={draft.injectEmulatedToPhysical} />
        <span class="wmc-bypass-label">Inject emulated frames to physical CAN</span>
        <span class="wmc-bypass-desc">Required for hybrid mode — hardware ECUs see software-generated frames.</span>
      </label>
    </section>

    <!-- ── Apply button ── -->
    <div class="wmc-footer">
      {#if isDirty}
        <span class="wmc-dirty-badge">Unsaved changes</span>
      {/if}
      <button
        class="wmc-apply-btn"
        disabled={applying || !$workModeReady}
        class:dirty={isDirty}
        on:click={applyConfig}
      >
        {applying ? "Applying…" : isDirty ? "▶ Apply" : "✓ Applied"}
      </button>
    </div>
  </div>
</div>

<style>
  .wmc-panel { display: flex; flex-direction: column; gap: 0; height: 100%; overflow-y: auto; }
  .wmc-header { padding: 12px 16px 8px; border-bottom: 1px solid var(--border, #333); }
  .wmc-title { font-weight: 700; font-size: 0.95rem; letter-spacing: 0.02em; }
  .wmc-sub { display: block; font-size: 0.75rem; color: var(--muted, #888); margin-top: 2px; }
  .wmc-body { display: flex; flex-direction: column; gap: 0; padding: 0 0 16px; }
  .wmc-section { padding: 12px 16px; border-bottom: 1px solid var(--border, #333); }
  .wmc-section-title { font-size: 0.8rem; font-weight: 600; color: var(--accent, #7eb8f7); text-transform: uppercase; letter-spacing: 0.06em; margin: 0 0 6px; display: flex; align-items: center; gap: 8px; }
  .wmc-badge { background: var(--accent, #7eb8f7); color: #000; font-size: 0.7rem; padding: 1px 6px; border-radius: 10px; font-weight: 700; }
  .wmc-hint { font-size: 0.73rem; color: var(--muted, #888); margin: 0 0 8px; }

  /* Mode buttons */
  .wmc-mode-grid { display: flex; flex-wrap: wrap; gap: 6px; }
  .wmc-mode-btn { background: var(--surface2, #1e1e2e); border: 1px solid var(--border, #333); color: var(--text, #ccc); padding: 5px 12px; border-radius: 6px; cursor: pointer; font-size: 0.78rem; transition: border-color 0.15s, background 0.15s; }
  .wmc-mode-btn:hover { border-color: var(--accent, #7eb8f7); }
  .wmc-mode-btn.active { background: var(--accent, #7eb8f7); color: #000; border-color: var(--accent, #7eb8f7); font-weight: 700; }

  /* ECU grid */
  .wmc-ecu-grid { display: flex; flex-direction: column; gap: 4px; }
  .wmc-ecu-row { display: grid; grid-template-columns: 20px 52px 1fr; align-items: center; gap: 8px; cursor: pointer; padding: 4px 6px; border-radius: 5px; font-size: 0.8rem; transition: background 0.1s; }
  .wmc-ecu-row:hover { background: var(--surface2, #1e1e2e); }
  .wmc-ecu-label { font-weight: 600; font-family: monospace; color: var(--text, #eee); }
  .wmc-ecu-desc { color: var(--muted, #888); font-size: 0.73rem; }

  /* Bypass grid */
  .wmc-bypass-grid { display: flex; flex-direction: column; gap: 4px; }
  .wmc-bypass-row { display: grid; grid-template-columns: 20px 90px 1fr; align-items: center; gap: 8px; cursor: pointer; padding: 4px 6px; border-radius: 5px; font-size: 0.8rem; transition: background 0.1s; }
  .wmc-bypass-row:hover { background: var(--surface2, #1e1e2e); }
  .wmc-bypass-label { font-weight: 600; color: var(--text, #eee); }
  .wmc-bypass-desc { color: var(--muted, #888); font-size: 0.73rem; }

  /* Footer */
  .wmc-footer { display: flex; align-items: center; gap: 12px; padding: 12px 16px 0; }
  .wmc-dirty-badge { font-size: 0.72rem; color: var(--warn, #f5a623); font-weight: 600; }
  .wmc-apply-btn { padding: 6px 20px; border-radius: 6px; border: 1px solid var(--border, #444); background: var(--surface2, #1e1e2e); color: var(--muted, #888); cursor: pointer; font-size: 0.82rem; transition: background 0.15s, color 0.15s, border-color 0.15s; }
  .wmc-apply-btn.dirty { background: var(--accent, #7eb8f7); color: #000; border-color: var(--accent, #7eb8f7); font-weight: 700; }
  .wmc-apply-btn:disabled { opacity: 0.5; cursor: not-allowed; }
</style>
