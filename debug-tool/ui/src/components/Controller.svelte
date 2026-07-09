<script lang="ts">
  import { onMount } from "svelte";
  import { get } from "svelte/store";
  import { sendFrame, simControllerInput } from "../lib/api";
  import type { Bus } from "../lib/can-decoder";
  import { BUSES, encodePayload } from "../lib/can-decoder";
  import { heldKeys, kbBus, kbEvent } from "../stores/keyboard";
  import { workMode } from "../stores/work-mode";
  import { telemetry } from "../stores/telemetry";

  // ── Tunable targets ──
  const TARGET_SPEED = 2000;       // mm/s  (W = forward, S = reverse)
  const TARGET_YAW_HIGH = 87;      // mrad/s (A = left, D = right on high bus)
  const TARGET_YAW_LOW = 50;       // deci°  (A = left, D = right on low bus)
  const BRAKE_PRESSURE = 5000;     // kPa (B = brake)

  // ── Loop state ──
  let selectedBus: Bus = "high";
  let active = false;
  let loopHandle: number | null = null;
  let intervalMs = 20;               // ~50 Hz, like Autoware
  let frameCount = 0;
  let lastTime = 0;
  let accumulator = 0;

  // ── Derived each tick from heldKeys ──
  let speed = 0;
  let yaw = 0;
  let brake = 0;
  let gear = 1;           // 0=N, 1=D, 2=S, 3=R
  let error = "";

  const GEARS = [
    { label: "N", value: 0 },
    { label: "D", value: 1 },
    { label: "S", value: 2 },
    { label: "R", value: 3 },
  ];
  $: gearLabel = GEARS.find(g => g.value === gear)?.label ?? "?";

  // ── ESTOP two-phase state ──
  let estopConfirming = false;
  let estopTimer: ReturnType<typeof setTimeout> | null = null;

  // ── CAN IDs per bus ──
  $: driveId = selectedBus === "high" ? "0x300" : "0x204";
  $: steerId = "0x169";  // low bus only
  $: brakeId = selectedBus === "high" ? "0x301" : "0x205";
  $: publishFrames = [
    { label: "Drive", bus: selectedBus, id: driveId, rate: `${Math.round(1000 / intervalMs)} Hz`, active: active },
    ...(selectedBus === "low" ? [{ label: "Steer", bus: "low" as Bus, id: steerId, rate: `${Math.round(1000 / intervalMs)} Hz`, active: active }] : []),
    { label: "Brake", bus: selectedBus, id: brakeId, rate: brake > 0 ? `${Math.round(1000 / intervalMs)} Hz` : "on demand", active: brake > 0 }
  ];

  // ── Display ──
  $: yawLabel = selectedBus === "high" ? "Yaw rate" : "Steer";
  $: yawUnit = selectedBus === "high" ? "mrad/s" : "deci°";
  $: yawDisplay = selectedBus === "high" ? yaw : (yaw / 10).toFixed(1);
  $: heldList = [...$heldKeys].join("+") || "—";
  $: hostSimulated = $workMode.simulatedEcus.includes("host");
  let odometer_m = 0;

  // ═══════════════════════════════════════════════════════════════
  // Game loop — poll heldKeys, derive control state, send CAN
  // ═══════════════════════════════════════════════════════════════

  // ── Per-tick send: batch all frames into a single Promise.all to cap concurrency ──
  let tickInFlight = false;

  function tick() {
    const keys = get(heldKeys);

    // Drive: W=forward, S=reverse, neither=neutral
    if (keys.has("w")) {
      speed = TARGET_SPEED;
      gear = 1;
      brake = 0;
    } else if (keys.has("s")) {
      speed = -TARGET_SPEED;
      gear = 2;
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

    // Skip this tick if the previous one is still in-flight (backpressure)
    if (tickInFlight) return;
    tickInFlight = true;

    const sends: Promise<unknown>[] = [];
    if (hostSimulated) {
      sends.push(simControllerInput({
        speed_mmps: speed,
        yaw_mrad_s: simYawMradS(),
        gear,
        brake_kpa: brake,
      }));
    } else {
      sends.push(buildDriveFrame());
      if (selectedBus === "low") sends.push(buildSteerFrame());
      if (brake > 0) sends.push(buildBrakeFrame());
    }

    Promise.all(sends)
      .catch((e: unknown) => { error = `Send failed: ${String(e)}`; })
      .finally(() => { tickInFlight = false; });

    frameCount++;
  }

  function buildDriveFrame(): Promise<unknown> {
    const vals: Record<string, number | boolean> =
      selectedBus === "high"
        ? { speed_mmps: speed, yaw_rate_mrad_s: yaw, gear }
        : { motor_speed_mmps: speed, gear };
    const enc = encodePayload(selectedBus, driveId, vals);
    return sendFrame({ bus: selectedBus, id: driveId, dlc: enc.dlc, data: enc.data });
  }

  function buildSteerFrame(): Promise<unknown> {
    const enc = encodePayload("low", steerId, { target_angle: yaw });
    return sendFrame({ bus: "low", id: steerId, dlc: enc.dlc, data: enc.data });
  }

  function buildBrakeFrame(): Promise<unknown> {
    const enc = encodePayload(selectedBus, brakeId, { brake_pressure_kpa: brake });
    return sendFrame({ bus: selectedBus, id: brakeId, dlc: enc.dlc, data: enc.data });
  }

  function simYawMradS(): number {
    return selectedBus === "high" ? yaw : Math.round((yaw / TARGET_YAW_LOW) * TARGET_YAW_HIGH);
  }

  function sendSimControllerState() {
    if (tickInFlight) return;
    tickInFlight = true;
    simControllerInput({
      speed_mmps: speed,
      yaw_mrad_s: simYawMradS(),
      gear,
      brake_kpa: brake,
    })
      .catch((e: unknown) => { error = `Sim controller update failed: ${String(e)}`; })
      .finally(() => { tickInFlight = false; });
  }

  async function sendEstopFrame(bus: Bus) {
    const enc = encodePayload(bus, "0x001", { estop: true });
    await sendFrame({ bus, id: "0x001", dlc: enc.dlc, data: enc.data, confirm_estop: true });
  }

  // ═══════════════════════════════════════════════════════════════
  // Loop control
  // ═══════════════════════════════════════════════════════════════

  function loop(time: number) {
    if (!active) return;
    if (lastTime === 0) lastTime = time;
    
    let dt = time - lastTime;
    if (dt > 1000) dt = 1000;
    
    accumulator += dt;
    lastTime = time;

    while (accumulator >= intervalMs) {
      tick();
      accumulator -= intervalMs;
    }
    
    loopHandle = requestAnimationFrame(loop);
  }

  function startLoop() {
    if (loopHandle) return;
    error = "";
    frameCount = 0;
    active = true;
    lastTime = 0;
    accumulator = 0;
    loopHandle = requestAnimationFrame(loop);
  }

  function stopLoop() {
    if (loopHandle) {
      cancelAnimationFrame(loopHandle);
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
    if (hostSimulated) {
      simControllerInput({ speed_mmps: 0, yaw_mrad_s: 0, gear: 1, brake_kpa: 0 })
        .catch((e: unknown) => { error = `Sim controller update failed: ${String(e)}`; });
      return;
    }

    // Publish zero-speed to bring vehicle to neutral
    if (selectedBus === "high") {
      const highEnc = encodePayload("high", "0x300", { speed_mmps: 0, yaw_rate_mrad_s: 0, gear: 1 });
      sendFrame({ bus: "high", id: "0x300", dlc: highEnc.dlc, data: highEnc.data }).catch((e: unknown) => { error = `Send failed: ${String(e)}`; });
      return;
    }

    const lowEnc = encodePayload("low", "0x204", { motor_speed_mmps: 0, gear: 1 });
    const steerEnc = encodePayload("low", "0x169", { target_angle: 0 });
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

    const stateTimer = setInterval(async () => {
      try {
        const res = await fetch("/api/sim/state");
        const json = await res.json();
        odometer_m = json.physics?.odometer_m ?? 0;
      } catch (err) {}
    }, 500);

    // Dead-man's switch: send zero-speed immediately when tab is backgrounded.
    // Prevents the vehicle holding speed after the browser throttles setInterval.
    function onVisibilityChange() {
      if (document.visibilityState === "hidden" && active) {
        sendZeroFrames();
      }
    }
    document.addEventListener("visibilitychange", onVisibilityChange);

    return () => {
      unsubBus();
      unsubEvent();
      clearInterval(stateTimer);
      document.removeEventListener("visibilitychange", onVisibilityChange);
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
        <span>Tgt Speed</span>
        <strong>{speed} <small>mm/s</small></strong>
        <div class="actual-feedback">Act: {$telemetry.motorSpeedKmh !== null ? ($telemetry.motorSpeedKmh * 1000 / 3.6).toFixed(0) : "0"} mm/s</div>
      </div>
      <div class="ctrl-metric" class:ctrl-active={yaw !== 0}>
        <span>Tgt {yawLabel}</span>
        <strong>{yawDisplay} <small>{yawUnit}</small></strong>
        <div class="actual-feedback">Act: {$telemetry.steerAngleDeg !== null ? $telemetry.steerAngleDeg.toFixed(1) : "0"} °</div>
      </div>
      <div class="ctrl-metric" class:ctrl-danger={brake > 0}>
        <span>Tgt Brake</span>
        <strong>{brake > 0 ? "ON" : "OFF"}</strong>
        <div class="actual-feedback">Act: {$telemetry.brakePressureMpa !== null ? ($telemetry.brakePressureMpa * 1000).toFixed(0) : "0"} kPa</div>
      </div>
      <div class="ctrl-metric">
        <span>Odometer</span>
        <strong>{odometer_m.toFixed(2)} <small>m</small></strong>
      </div>
      <div class="ctrl-metric">
        <span>Keys held</span>
        <strong style="font-size:1rem;">{heldList}</strong>
      </div>
    </div>

    <!-- Gear selector -->
    <div class="gear-strip">
      <span class="kb-head">Gear</span>
      <div class="gear-btns">
        {#each GEARS as g}
          <button
            class="gear-btn"
            class:active={gear === g.value}
            type="button"
            on:click={() => gear = g.value}
          >
            {g.label}
          </button>
        {/each}
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

  <div class="panel">
    <div class="panel-title">
      <h2>Publish Contract</h2>
      <span>{selectedBus.toUpperCase()} bus</span>
    </div>
    <div class="contract-table">
      <div class="contract-head">
        <span>Signal</span>
        <span>Frame</span>
        <span>Rate</span>
        <span>State</span>
      </div>
      {#each publishFrames as frame}
        <div class="contract-row" class:active={frame.active}>
          <strong>{frame.label}</strong>
          <span class="mono">{frame.bus}:{frame.id}</span>
          <span>{frame.rate}</span>
          <span>{frame.active ? "publishing" : "armed"}</span>
        </div>
      {/each}
    </div>

    <div class="safety-grid">
      <div>
        <span>Zero command</span>
        <strong>Esc / Zero</strong>
      </div>
      <div>
        <span>ESTOP guard</span>
        <strong>Space x2 / 1 s</strong>
      </div>
      <div>
        <span>Focus loss</span>
        <strong>keys cleared</strong>
      </div>
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
      <div class="ctrl-log-row"><span>Gear</span><strong>{gearLabel}</strong></div>
    </div>
  </div>
</section>

<style>
  .ctrl-state {
    display: grid;
    gap: 10px;
    grid-template-columns: repeat(5, minmax(0, 1fr));
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

  .actual-feedback {
    color: var(--ok);
    font-size: 0.75rem;
    font-weight: 600;
    margin-top: 6px;
    font-family: "Cascadia Mono", "SFMono-Regular", Consolas, monospace;
  }

  .ctrl-active {
    border-color: var(--accent);
  }

  .ctrl-danger {
    border: 1px solid var(--err);
  }
  .ctrl-danger strong { color: var(--err); }

  .gear-strip {
    align-items: center;
    display: flex;
    gap: 10px;
    margin-bottom: 14px;
  }
  .gear-btns {
    display: flex;
    gap: 4px;
  }
  .gear-btn {
    background: var(--panel);
    border: 1px solid var(--panel-border);
    border-radius: 5px;
    color: var(--muted);
    cursor: pointer;
    font-family: "Cascadia Mono", "SFMono-Regular", Consolas, monospace;
    font-size: 0.9rem;
    font-weight: 800;
    min-width: 36px;
    padding: 4px 0;
    text-align: center;
    transition: background 0.12s, color 0.12s, border-color 0.12s;
  }
  .gear-btn:hover { background: var(--bg); color: var(--fg); }
  .gear-btn.active {
    border-color: var(--accent);
    background: color-mix(in srgb, var(--accent) 15%, var(--bg));
    color: var(--accent);
  }

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

  .contract-table {
    display: grid;
    gap: 0;
  }

  .contract-head,
  .contract-row {
    align-items: center;
    display: grid;
    gap: 8px;
    grid-template-columns: minmax(72px, 0.8fr) minmax(88px, 1fr) minmax(70px, 0.8fr) minmax(80px, 0.9fr);
    min-width: 0;
  }

  .contract-head {
    background: var(--bg);
    border-bottom: 1px solid var(--panel-border);
    padding: 9px 10px;
  }

  .contract-head span,
  .safety-grid span {
    color: var(--muted);
    font-size: 0.72rem;
    font-weight: 700;
    text-transform: uppercase;
  }

  .contract-row {
    border-bottom: 1px solid var(--panel-border);
    border-left: 3px solid var(--muted);
    padding: 10px;
  }

  .contract-row.active {
    border-left-color: var(--ok);
  }

  .contract-row span,
  .contract-row strong {
    font-size: 0.82rem;
    min-width: 0;
    overflow-wrap: anywhere;
  }

  .safety-grid {
    display: grid;
    gap: 8px;
    grid-template-columns: repeat(3, minmax(0, 1fr));
    margin-top: 14px;
  }

  .safety-grid div {
    background: var(--bg);
    border: 1px solid var(--panel-border);
    border-radius: 6px;
    min-height: 64px;
    padding: 10px;
  }

  .safety-grid strong {
    display: block;
    font-size: 0.9rem;
    margin-top: 8px;
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

    .contract-head,
    .contract-row,
    .safety-grid {
      grid-template-columns: 1fr;
    }
  }

  @media (max-width: 680px) {
    .ctrl-state {
      grid-template-columns: 1fr;
    }
  }
</style>
