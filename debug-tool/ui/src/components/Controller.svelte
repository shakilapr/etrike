<script lang="ts">
  import { onMount } from "svelte";
  import { sendFrame } from "../lib/api";
  import type { Bus } from "../lib/can-decoder";
  import { BUSES, encodePayload } from "../lib/can-decoder";
  import { heldKeys, kbBus, kbEvent } from "../stores/keyboard";

  // ── Tunable targets ──
  const TARGET_SPEED = 2000;       // mm/s  (W = forward, S = reverse)
  const TARGET_YAW_HIGH = 87;      // mrad/s (A = left, D = right on high bus)
  const TARGET_YAW_LOW = 50;       // deci°  (A = left, D = right on low bus)
  const BRAKE_PRESSURE = 5000;     // kPa (B = brake)

  // ── Loop state ──
  let selectedBus: Bus = "high";
  let active = false;
  let loopHandle: ReturnType<typeof setInterval> | null = null;
  let intervalMs = 20;               // ~50 Hz, like Autoware
  let frameCount = 0;

  // ── Derived each tick from heldKeys ──
  let speed = 0;
  let yaw = 0;
  let brake = 0;
  let gear = 1;
  let error = "";

  // ── ESTOP two-phase state ──
  let estopConfirming = false;
  let estopTimer: ReturnType<typeof setTimeout> | null = null;

  // ── CAN IDs per bus ──
  $: driveId = selectedBus === "high" ? "0x300" : "0x204";
  $: steerId = "0x169";  // low bus only
  $: brakeId = selectedBus === "high" ? "0x301" : "0x205";

  // ── Display ──
  $: yawLabel = selectedBus === "high" ? "Yaw rate" : "Steer";
  $: yawUnit = selectedBus === "high" ? "mrad/s" : "deci°";
  $: yawDisplay = selectedBus === "high" ? yaw : (yaw / 10).toFixed(1);
  $: heldNow = $heldKeys;
  $: heldList = [...heldNow].join("+") || "—";

  // ═══════════════════════════════════════════════════════════════
  // Game loop — poll heldKeys, derive control state, send CAN
  // ═══════════════════════════════════════════════════════════════

  function tick() {
    const keys = heldNow; // reactive — stays fresh via Svelte subscription

    // Drive: W=forward, S=reverse, neither=neutral
    if (keys.has("w")) {
      speed = TARGET_SPEED;
      brake = 0;
    } else if (keys.has("s")) {
      speed = -TARGET_SPEED;
      brake = 0;
    } else {
      speed = 0;
    }

    // Yaw / steer: A=left, D=right, neither=center
    if (keys.has("a")) {
      yaw = -(selectedBus === "high" ? TARGET_YAW_HIGH : TARGET_YAW_LOW);
    } else if (keys.has("d")) {
      yaw = selectedBus === "high" ? TARGET_YAW_HIGH : TARGET_YAW_LOW;
    } else {
      yaw = 0;
    }

    // Brake: B held → brake on (overrides drive)
    if (keys.has("b")) {
      brake = BRAKE_PRESSURE;
      speed = 0;
    }

    // Encode & send
    sendDriveFrame();
    if (selectedBus === "low") sendSteerFrame();
    if (brake > 0) sendBrakeFrame();

    frameCount++;
  }

  function sendDriveFrame() {
    const vals: Record<string, number | boolean> =
      selectedBus === "high"
        ? { speed_mmps: speed, yaw_rate_mrad_s: yaw, gear }
        : { motor_speed_mmps: speed, gear };
    const enc = encodePayload(selectedBus, driveId, vals);
    sendFrame({ bus: selectedBus, id: driveId, dlc: enc.dlc, data: enc.data }).catch((e: unknown) => { error = `Drive send failed: ${String(e)}`; });
  }

  function sendSteerFrame() {
    const enc = encodePayload("low", steerId, { target_angle: yaw });
    sendFrame({ bus: "low", id: steerId, dlc: enc.dlc, data: enc.data }).catch((e: unknown) => { error = `Send failed: ${String(e)}`; });
  }

  function sendBrakeFrame() {
    const enc = encodePayload(selectedBus, brakeId, { brake_pressure_kpa: brake });
    sendFrame({ bus: selectedBus, id: brakeId, dlc: enc.dlc, data: enc.data }).catch((e: unknown) => { error = `Send failed: ${String(e)}`; });
  }

  async function sendEstopFrame(bus: Bus) {
    const enc = encodePayload(bus, "0x001", { estop: true });
    await sendFrame({ bus, id: "0x001", dlc: enc.dlc, data: enc.data, confirm_estop: true });
  }

  // ═══════════════════════════════════════════════════════════════
  // Loop control
  // ═══════════════════════════════════════════════════════════════

  function startLoop() {
    if (loopHandle) return;
    error = "";
    frameCount = 0;
    active = true;
    loopHandle = setInterval(tick, intervalMs);
  }

  function stopLoop() {
    if (loopHandle) {
      clearInterval(loopHandle);
      loopHandle = null;
    }
    // Send one final zero-speed frame so the vehicle stops
    sendZeroFrames();
    active = false;
  }

  function restartLoop() {
    stopLoop();
    startLoop();
  }

  function sendZeroFrames() {
    // Publish zero-speed to bring vehicle to neutral
    const valsHigh = { speed_mmps: 0, yaw_rate_mrad_s: 0, gear: 1 };
    const valsLow = { motor_speed_mmps: 0, gear: 1 };
    const valsSteer = { target_angle: 0 };

    const highEnc = encodePayload("high", "0x300", valsHigh);
    const lowEnc = encodePayload("low", "0x204", valsLow);
    const steerEnc = encodePayload("low", "0x169", valsSteer);

    sendFrame({ bus: "high", id: "0x300", dlc: highEnc.dlc, data: highEnc.data }).catch((e: unknown) => { error = `Send failed: ${String(e)}`; });
    sendFrame({ bus: "low", id: "0x204", dlc: lowEnc.dlc, data: lowEnc.data }).catch((e: unknown) => { error = `Send failed: ${String(e)}`; });
    sendFrame({ bus: "low", id: "0x169", dlc: steerEnc.dlc, data: steerEnc.data }).catch((e: unknown) => { error = `Send failed: ${String(e)}`; });
  }

  function chooseBus(bus: Bus) {
    selectedBus = bus;
    kbBus.set(bus);
    speed = 0;
    yaw = 0;
    brake = 0;
    if (active) sendZeroFrames();
  }

  // ═══════════════════════════════════════════════════════════════
  // Subscriptions
  // ═══════════════════════════════════════════════════════════════

  onMount(() => {
    // Bus switching via Tab
    const unsubBus = kbBus.subscribe((b) => {
      if (b !== selectedBus) chooseBus(b);
    });

    // Discrete actions: ESTOP, zero-all
    const unsubEvent = kbEvent.subscribe((evt) => {
      if (!evt) return;
      handleDiscrete(evt.bus, evt.action);
    });

    return () => {
      unsubBus();
      unsubEvent();
      stopLoop();
    };
  });

  function handleDiscrete(bus: Bus, action: { type: string }) {
    switch (action.type) {
      case "zero_all":
        speed = 0;
        yaw = 0;
        brake = 0;
        gear = 1;
        error = "";
        if (active) sendZeroFrames();
        break;

      case "estop_confirm":
        error = "Press Space again within 1s to confirm ESTOP";
        estopConfirming = true;
        if (estopTimer) clearTimeout(estopTimer);
        estopTimer = setTimeout(() => {
          estopConfirming = false;
          error = "";
        }, 1000);
        break;

      case "estop_send":
        if (estopTimer) clearTimeout(estopTimer);
        estopConfirming = false;
        error = "";
        stopLoop();
        sendEstopFrame(bus);
        break;
    }
  }
</script>

<!-- ═══════════════════════════════════════════════════════════════ -->
<!-- UI                                                                 -->
<!-- ═══════════════════════════════════════════════════════════════ -->

<section class="injector-layout">
  <div class="panel">
    <div class="panel-title">
      <h2>Controller</h2>
      <span class="mono">{active ? "● LIVE" : "○ idle"} · {frameCount} frames</span>
    </div>

    <!-- Bus selector -->
    <div class="bus-tabs">
      {#each BUSES as bus}
        <button class:active={selectedBus === bus} type="button" on:click={() => chooseBus(bus)}>
          {bus.toUpperCase()} Bus
        </button>
      {/each}
    </div>

    <!-- Control state — mirrors what the loop is sending -->
    <div class="ctrl-state">
      <div class="ctrl-metric">
        <span>Speed</span>
        <strong>{speed} <small>mm/s</small></strong>
      </div>
      <div class="ctrl-metric" class:ctrl-active={yaw !== 0}>
        <span>{yawLabel}</span>
        <strong>{yawDisplay} <small>{yawUnit}</small></strong>
      </div>
      <div class="ctrl-metric" class:ctrl-danger={brake > 0}>
        <span>Brake</span>
        <strong>{brake > 0 ? "ON" : "OFF"}</strong>
      </div>
      <div class="ctrl-metric">
        <span>Keys held</span>
        <strong style="font-size:1rem;">{heldList}</strong>
      </div>
    </div>

    <!-- Active CAN IDs -->
    <div class="ctrl-frames">
      <span class="kb-head">Publishing</span>
      <div class="ctrl-frame-tags">
        <span class="bus-tag">{selectedBus}:{driveId}</span>
        {#if selectedBus === "low"}
          <span class="bus-tag">low:{steerId}</span>
        {/if}
        {#if brake > 0}
          <span class="bus-tag">{selectedBus}:{brakeId}</span>
        {/if}
      </div>
    </div>

    <!-- Interval -->
    <div class="periodic-controls">
      <label class="field">
        <span>Interval (ms)</span>
        <input bind:value={intervalMs} min="5" max="1000" type="number" on:change={restartLoop} />
      </label>
      <div class="field">
        <span>Rate</span>
        <span class="mono" style="padding-top:8px">{active ? (1000 / intervalMs).toFixed(0) + " Hz" : "—"}</span>
      </div>
    </div>

    {#if error}
      <div class="alert" class:estop-warn={estopConfirming}>{error}</div>
    {/if}

    <div class="button-row">
      <button disabled={active} type="button" on:click={startLoop}>▶ Start</button>
      <button disabled={!active} type="button" on:click={stopLoop}>⏹ Stop</button>
      <button type="button" on:click={() => { speed = 0; yaw = 0; brake = 0; gear = 1; }}>
        ↺ Zero
      </button>
    </div>

    <!-- Keyboard hints -->
    <div class="kb-card">
      <span class="kb-head">{$kbBus.toUpperCase()} Bus Controls</span>
      <div class="kb-grid">
        <span><kbd>W</kbd><kbd>S</kbd> Forward / Reverse</span>
        <span><kbd>A</kbd><kbd>D</kbd> Turn Left / Right</span>
        <span><kbd>B</kbd> Brake (hold)</span>
        <span><kbd>Space×2</kbd> ESTOP</span>
        <span><kbd>Esc</kbd> Zero all</span>
        <span><kbd>Tab</kbd> Switch bus</span>
      </div>
    </div>
  </div>

  <!-- Info panel -->
  <div class="panel">
    <div class="panel-title">
      <h2>How it works</h2>
    </div>
    <div style="color:var(--muted);font-size:0.82rem;line-height:1.6;">
      <p>Mimics how <strong>Autoware</strong> continuously publishes control commands.</p>
      <p><strong>Hold</strong> a key to activate its control; <strong>release</strong> to return to neutral. The loop polls which keys are held and sends the corresponding CAN frames at the configured interval — just like a game engine's input→update→publish cycle.</p>
      <ul style="padding-left:16px;display:grid;gap:6px;">
        <li><strong>HIGH</strong>: 0x300 (drive) + 0x301 (brake)</li>
        <li><strong>LOW</strong>: 0x204 (speed) + 0x169 (steer) + 0x205 (brake)</li>
      </ul>
    </div>
  </div>

  <!-- Status log -->
  <div class="panel history-panel">
    <div class="panel-title">
      <h2>Status</h2>
    </div>
    <div class="ctrl-log">
      <div class="ctrl-log-row"><span>Loop</span><strong>{active ? "Running" : "Stopped"}</strong></div>
      <div class="ctrl-log-row"><span>Bus</span><strong>{selectedBus.toUpperCase()}</strong></div>
      <div class="ctrl-log-row"><span>Interval</span><strong>{intervalMs} ms</strong></div>
      <div class="ctrl-log-row"><span>Frames sent</span><strong>{frameCount}</strong></div>
      <div class="ctrl-log-row"><span>Keys</span><strong>{heldList}</strong></div>
      <div class="ctrl-log-row"><span>Speed</span><strong>{speed} mm/s</strong></div>
      <div class="ctrl-log-row"><span>{yawLabel}</span><strong>{yawDisplay} {yawUnit}</strong></div>
      <div class="ctrl-log-row"><span>Brake</span><strong>{brake > 0 ? `${brake} kPa` : "Released"}</strong></div>
      <div class="ctrl-log-row"><span>Gear</span><strong>{gear}</strong></div>
    </div>
  </div>
</section>

<style>
  .ctrl-state {
    display: grid;
    gap: 10px;
    grid-template-columns: repeat(4, minmax(0, 1fr));
    margin-bottom: 16px;
  }

  .ctrl-metric {
    background: var(--bg);
    border-radius: 6px;
    min-height: 68px;
    padding: 12px;
    border: 1px solid transparent;
  }

  .ctrl-metric span {
    color: var(--muted);
    display: block;
    font-size: 0.72rem;
    font-weight: 700;
    text-transform: uppercase;
  }

  .ctrl-metric strong {
    display: block;
    font-size: 1.5rem;
    margin-top: 8px;
  }

  .ctrl-metric strong small {
    color: var(--muted);
    font-size: 0.72rem;
    font-weight: 400;
  }

  .ctrl-active {
    border-color: var(--accent);
  }

  .ctrl-danger {
    border: 1px solid var(--err);
  }
  .ctrl-danger strong { color: var(--err); }

  .ctrl-frames {
    border-top: 1px solid var(--panel-border);
    padding-top: 12px;
    margin-bottom: 12px;
  }

  .ctrl-frame-tags {
    display: flex;
    gap: 6px;
    flex-wrap: wrap;
  }

  .ctrl-log {
    display: grid;
    gap: 4px;
  }

  .ctrl-log-row {
    align-items: center;
    display: flex;
    justify-content: space-between;
    padding: 6px 0;
    border-bottom: 1px solid var(--panel-border);
  }

  .ctrl-log-row span {
    color: var(--muted);
    font-size: 0.82rem;
  }

  .ctrl-log-row strong {
    font-size: 0.82rem;
  }

  .estop-warn {
    background: color-mix(in srgb, var(--warn) 20%, var(--bg));
    border-color: var(--warn);
    color: var(--warn);
  }

  @media (max-width: 980px) {
    .ctrl-state {
      grid-template-columns: repeat(2, minmax(0, 1fr));
    }
  }

  @media (max-width: 680px) {
    .ctrl-state {
      grid-template-columns: 1fr;
    }
  }
</style>
