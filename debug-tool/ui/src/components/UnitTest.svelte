<script lang="ts">
  import { sendFrame, startPeriodic, stopPeriodic } from "../lib/api";
  import type { Bus, CanField, CanFrame, CanMessageDef } from "../lib/can-decoder";
  import { encodePayload, findMessage, formatBytes, formatDecoded, frameTime } from "../lib/can-decoder";
  import { commandAcks, latestById } from "../stores/can";

  export let ids: CanMessageDef[] = [];

  interface UnitCommand {
    bus: Bus;
    id: string;
    label: string;
    defaults: Record<string, number | boolean>;
    intervalMs: number;
  }

  interface FeedbackRef {
    bus: Bus;
    id: string;
    label: string;
  }

  interface UnitProfile {
    id: string;
    name: string;
    role: string;
    commands: UnitCommand[];
    feedback: FeedbackRef[];
  }

  const profiles: UnitProfile[] = [
    {
      id: "eps",
      name: "EPS-C Steering",
      role: "Low bus actuator: target angle command and steering status/fault feedback.",
      commands: [
        { bus: "low", id: "0x169", label: "Steer target", intervalMs: 20, defaults: { alignment_enable: false, control_enable: true, target_angle: 0, target_speed: 328, rolling_counter: 1, checksum: 0 } }
      ],
      feedback: [
        { bus: "low", id: "0x201", label: "Steering status" },
        { bus: "low", id: "0x202", label: "Steering faults" },
        { bus: "low", id: "0x203", label: "Steering version" },
        { bus: "low", id: "0x6FA", label: "Steering test telemetry" }
      ]
    },
    {
      id: "seb",
      name: "SEB Brake",
      role: "Low bus actuator: brake request command and hydraulic status/fault feedback.",
      commands: [
        { bus: "low", id: "0x7B9", label: "Brake request", intervalMs: 20, defaults: { align_enable: false, control_enable: true, control_mode: 0, auto_brake: false, stroke_req: 0, pressure_req: 0, rolling_counter: 1, checksum: 0 } },
        { bus: "low", id: "0x205", label: "RT brake pressure", intervalMs: 20, defaults: { brake_pressure_kpa: 0 } }
      ],
      feedback: [
        { bus: "low", id: "0x721", label: "Brake status" },
        { bus: "low", id: "0x731", label: "Brake faults" },
        { bus: "low", id: "0x741", label: "Brake version" },
        { bus: "low", id: "0x6FB", label: "Brake test telemetry" }
      ]
    },
    {
      id: "mtr",
      name: "MTR Motor",
      role: "Low bus motor node: speed/gear command with speed and gear feedback.",
      commands: [
        { bus: "low", id: "0x204", label: "Motor drive command", intervalMs: 10, defaults: { motor_speed_mmps: 0, gear: 1 } },
        { bus: "low", id: "0x110", label: "Mode command", intervalMs: 200, defaults: { mode: 0 } }
      ],
      feedback: [
        { bus: "low", id: "0x120", label: "Throttle/speed status" },
        { bus: "low", id: "0x206", label: "Motor feedback" },
        { bus: "low", id: "0x7FE", label: "SYS heartbeat" },
        { bus: "low", id: "0x600", label: "Diagnostics" }
      ]
    },
    {
      id: "sys",
      name: "SYS Safety/Body",
      role: "Low bus safety/body node: mode, lights, safety status, heartbeat, diagnostics.",
      commands: [
        { bus: "low", id: "0x110", label: "Mode command", intervalMs: 200, defaults: { mode: 0 } },
        { bus: "low", id: "0x302", label: "Light command", intervalMs: 200, defaults: { left_turn: false, right_turn: false, brake_light: false, headlight: false } },
        { bus: "low", id: "0x001", label: "ESTOP event", intervalMs: 1000, defaults: {} }
      ],
      feedback: [
        { bus: "low", id: "0x011", label: "Safety status" },
        { bus: "low", id: "0x600", label: "Diagnostics" },
        { bus: "low", id: "0x7FE", label: "SYS heartbeat" }
      ]
    },
    {
      id: "rt",
      name: "RT Gateway",
      role: "High-to-low pipeline: host commands in, low-bus actuator commands and RT state out.",
      commands: [
        { bus: "high", id: "0x300", label: "Host drive command", intervalMs: 20, defaults: { speed_mmps: 0, yaw_rate_mrad_s: 0, gear: 1 } },
        { bus: "high", id: "0x301", label: "Host brake request", intervalMs: 50, defaults: { brake_pressure_kpa: 0 } },
        { bus: "high", id: "0x7FC", label: "Jetson heartbeat", intervalMs: 500, defaults: { alive_ctr: 1 } }
      ],
      feedback: [
        { bus: "high", id: "0x210", label: "RT state" },
        { bus: "high", id: "0x7FD", label: "RT heartbeat" },
        { bus: "low", id: "0x204", label: "Low drive output" },
        { bus: "low", id: "0x169", label: "Low steer output" },
        { bus: "low", id: "0x7B9", label: "Low brake output" }
      ]
    },
    {
      id: "host",
      name: "Jetson Host Interface",
      role: "High bus host-side commands: drive, brake, lights, obstacle distance, heartbeat.",
      commands: [
        { bus: "high", id: "0x300", label: "Drive command", intervalMs: 20, defaults: { speed_mmps: 0, yaw_rate_mrad_s: 0, gear: 1 } },
        { bus: "high", id: "0x301", label: "Brake request", intervalMs: 50, defaults: { brake_pressure_kpa: 0 } },
        { bus: "high", id: "0x302", label: "Light command", intervalMs: 200, defaults: { left_turn: false, right_turn: false, brake_light: false, headlight: false } },
        { bus: "high", id: "0x400", label: "Obstacle distance", intervalMs: 100, defaults: { distance_mm: 4294967295 } },
        { bus: "high", id: "0x7FC", label: "Heartbeat", intervalMs: 500, defaults: { alive_ctr: 1 } }
      ],
      feedback: [
        { bus: "high", id: "0x210", label: "RT state" },
        { bus: "high", id: "0x011", label: "Safety status" },
        { bus: "high", id: "0x120", label: "Speed status" },
        { bus: "high", id: "0x206", label: "Motor feedback" }
      ]
    }
  ];

  let selectedUnitId = profiles[0].id;
  let selectedCommandIndex = 0;
  let values: Record<string, number | boolean> = { ...profiles[0].commands[0].defaults };
  let intervalMs = profiles[0].commands[0].intervalMs;
  let count = 500;
  let confirmEstop = false;
  let pending = false;
  let error = "";

  $: unit = profiles.find((profile) => profile.id === selectedUnitId) ?? profiles[0];
  $: command = unit.commands[selectedCommandIndex] ?? unit.commands[0];
  $: definition = command ? findMessage(command.bus, command.id) : undefined;
  $: encoded = command ? encodePayload(command.bus, command.id, values) : { dlc: 0, data: [] };
  $: feedbackRows = unit.feedback.map((item) => ({
    ...item,
    definition: ids.find((message) => message.bus === item.bus && message.id === item.id),
    frame: $latestById[`${item.bus}:${item.id}`]
  }));

  function selectUnit(id: string) {
    selectedUnitId = id;
    selectedCommandIndex = 0;
    loadCommandDefaults();
  }

  function selectCommand(index: number) {
    selectedCommandIndex = index;
    loadCommandDefaults();
  }

  function loadCommandDefaults() {
    const nextCommand = unit.commands[selectedCommandIndex] ?? unit.commands[0];
    values = { ...(nextCommand?.defaults ?? {}) };
    intervalMs = nextCommand?.intervalMs ?? 20;
    confirmEstop = false;
    error = "";
  }

  function updateField(field: CanField, value: string | boolean) {
    values = {
      ...values,
      [field.key]: field.kind === "boolean" ? Boolean(value) : Number(value)
    };
  }

  function frameAge(frame?: CanFrame): string {
    if (!frame) return "--";
    const stamp = frame.ts_real ?? frame.ts;
    const age = Math.max(Date.now() / 1000 - stamp, 0);
    if (age < 1) return `${Math.round(age * 1000)} ms`;
    if (age < 60) return `${age.toFixed(1)} s`;
    return `${Math.round(age)} s`;
  }

  async function runCommand(action: "send" | "start" | "stop") {
    if (!command) return;
    pending = true;
    error = "";
    try {
      if (action === "send") {
        await sendFrame({ bus: command.bus, id: command.id, dlc: encoded.dlc, data: encoded.data, confirm_estop: confirmEstop });
      } else if (action === "start") {
        await startPeriodic({ bus: command.bus, id: command.id, dlc: encoded.dlc, data: encoded.data, interval_ms: intervalMs, count, confirm_estop: confirmEstop });
      } else {
        await stopPeriodic(command.bus, command.id);
      }
    } catch (err) {
      error = err instanceof Error ? err.message : String(err);
    } finally {
      pending = false;
    }
  }
</script>

<section class="unit-test-layout">
  <aside class="panel unit-list">
    <div class="panel-title">
      <h2>Unit Under Test</h2>
      <span>{profiles.length}</span>
    </div>
    <div class="unit-buttons">
      {#each profiles as profile}
        <button class:active={profile.id === selectedUnitId} type="button" on:click={() => selectUnit(profile.id)}>
          <strong>{profile.name}</strong>
          <span>{profile.role}</span>
        </button>
      {/each}
    </div>
  </aside>

  <section class="panel command-panel">
    <div class="panel-title">
      <h2>{unit.name}</h2>
      <span>{command?.bus.toUpperCase()} {command?.id}</span>
    </div>

    <div class="command-tabs">
      {#each unit.commands as item, index}
        <button class:active={index === selectedCommandIndex} type="button" on:click={() => selectCommand(index)}>
          <span class="mono">{item.bus}:{item.id}</span>
          <strong>{item.label}</strong>
        </button>
      {/each}
    </div>

    {#if definition}
      <div class="unit-form">
        {#each definition.fields as field}
          <label class="field">
            <span>{field.label}{field.unit ? ` (${field.unit})` : ""}</span>
            {#if field.kind === "boolean"}
              <input checked={Boolean(values[field.key])} type="checkbox" on:change={(event) => updateField(field, event.currentTarget.checked)} />
            {:else if field.kind === "enum"}
              <select value={String(values[field.key] ?? field.options?.[0]?.value ?? 0)} on:change={(event) => updateField(field, event.currentTarget.value)}>
                {#each field.options ?? [] as option}
                  <option value={option.value}>{option.label}</option>
                {/each}
              </select>
            {:else}
              <input max={field.max} min={field.min} step={field.step ?? 1} type="number" value={String(values[field.key] ?? 0)} on:input={(event) => updateField(field, event.currentTarget.value)} />
            {/if}
          </label>
        {/each}
      </div>
    {/if}

    {#if command?.id === "0x001"}
      <label class="confirm-row">
        <input bind:checked={confirmEstop} type="checkbox" />
        <span>Confirm ESTOP injection</span>
      </label>
    {/if}

    <div class="encoded-row">
      <div>
        <span>Encoded payload</span>
        <strong class="mono">{formatBytes(encoded.data)}</strong>
      </div>
      <label class="field">
        <span>Interval (ms)</span>
        <input bind:value={intervalMs} min="1" max="60000" type="number" />
      </label>
      <label class="field">
        <span>Count</span>
        <input bind:value={count} min="1" max="50000" type="number" />
      </label>
    </div>

    {#if error}
      <div class="alert">{error}</div>
    {/if}

    <div class="button-row">
      <button disabled={pending || !command} type="button" on:click={() => runCommand("send")}>Send Once</button>
      <button disabled={pending || !command} type="button" on:click={() => runCommand("start")}>Start Periodic</button>
      <button disabled={pending || !command} type="button" on:click={() => runCommand("stop")}>Stop Periodic</button>
    </div>
  </section>

  <section class="panel feedback-panel">
    <div class="panel-title">
      <h2>Feedback</h2>
      <span>{feedbackRows.filter((row) => row.frame).length}/{feedbackRows.length} present</span>
    </div>

    <div class="feedback-table">
      <div class="feedback-head">
        <span>Signal</span>
        <span>Freshness</span>
        <span>Payload</span>
        <span>Decoded</span>
      </div>
      {#each feedbackRows as row}
        <div class="feedback-row" class:active={Boolean(row.frame)}>
          <div>
            <span class="bus-tag">{row.bus}</span>
            <strong>{row.id} {row.label}</strong>
            <small>{row.frame ? frameTime(row.frame) : "--"}</small>
          </div>
          <span>{frameAge(row.frame)}</span>
          <code>{row.frame ? formatBytes(row.frame.data) : "--"}</code>
          <code>{row.frame ? formatDecoded(row.frame.decoded) : "--"}</code>
        </div>
      {/each}
    </div>
  </section>

  <section class="panel ack-panel">
    <div class="panel-title">
      <h2>Command Acks</h2>
      <span>{$commandAcks.length}</span>
    </div>
    <div class="ack-stream">
      {#each $commandAcks.slice(0, 8) as ack}
        <pre>{JSON.stringify(ack, null, 2)}</pre>
      {:else}
        <div class="empty-state">No command acknowledgements yet.</div>
      {/each}
    </div>
  </section>
</section>

<style>
  .unit-test-layout {
    align-items: start;
    display: grid;
    gap: 14px;
    grid-template-columns: 280px minmax(420px, 1fr) minmax(420px, 1.15fr);
  }

  .unit-test-layout > .panel,
  .unit-test-layout label,
  .unit-test-layout button,
  .unit-test-layout span,
  .unit-test-layout strong,
  .unit-test-layout code,
  .unit-test-layout pre {
    min-width: 0;
  }

  .unit-list,
  .ack-panel {
    max-height: calc(100vh - 178px);
    overflow: auto;
  }

  .command-panel,
  .feedback-panel {
    overflow: hidden;
  }

  .unit-buttons,
  .command-tabs,
  .feedback-table,
  .ack-stream {
    display: grid;
    gap: 8px;
  }

  .unit-buttons button,
  .command-tabs button {
    align-items: start;
    display: grid;
    gap: 4px;
    height: auto;
    justify-items: start;
    min-height: 62px;
    padding: 10px 12px;
    text-align: left;
  }

  .unit-buttons button.active,
  .command-tabs button.active {
    background: var(--accent-dim);
    border-color: var(--accent);
  }

  .unit-buttons span,
  .command-tabs span,
  .encoded-row span,
  .feedback-head span,
  .feedback-row small {
    color: var(--muted);
    font-size: 0.74rem;
  }

  .unit-form,
  .encoded-row {
    display: grid;
    gap: 12px;
    grid-template-columns: repeat(auto-fit, minmax(132px, 1fr));
    margin: 14px 0;
  }

  .encoded-row {
    align-items: end;
    grid-template-columns: minmax(0, 1fr) minmax(92px, 120px) minmax(92px, 120px);
  }

  .encoded-row > div {
    background: var(--bg);
    border: 1px solid var(--panel-border);
    border-radius: 6px;
    min-height: 66px;
    padding: 12px;
  }

  .encoded-row strong {
    display: block;
    margin-top: 8px;
    overflow-wrap: anywhere;
  }

  .feedback-head,
  .feedback-row {
    display: grid;
    gap: 10px;
    grid-template-columns: minmax(132px, 1fr) minmax(70px, 0.45fr) minmax(96px, 0.7fr) minmax(0, 1.35fr);
    min-width: 0;
  }

  .feedback-head {
    background: var(--bg);
    border-bottom: 1px solid var(--panel-border);
    padding: 10px 12px;
  }

  .feedback-row {
    border-bottom: 1px solid var(--panel-border);
    border-left: 3px solid var(--muted);
    min-height: 58px;
    padding: 10px 12px;
  }

  .feedback-row.active {
    border-left-color: var(--ok);
  }

  .feedback-row strong {
    display: block;
    font-size: 0.86rem;
    margin-top: 6px;
  }

  .feedback-row code {
    font-family: "Cascadia Mono", "SFMono-Regular", Consolas, monospace;
    font-size: 0.78rem;
    overflow-wrap: anywhere;
    white-space: normal;
  }

  .confirm-row {
    margin-bottom: 12px;
  }

  .ack-stream pre {
    font-size: 0.76rem;
    margin: 0;
  }

  .empty-state {
    color: var(--muted);
    padding: 14px 0;
  }

  @media (max-width: 1200px) {
    .unit-test-layout {
      grid-template-columns: 1fr;
    }

    .unit-form,
    .encoded-row,
    .feedback-head,
    .feedback-row {
      grid-template-columns: 1fr;
    }

    .unit-list,
    .ack-panel {
      max-height: none;
    }
  }

  @media (max-width: 560px) {
    .encoded-row,
    .feedback-head,
    .feedback-row {
      gap: 8px;
    }
  }
</style>
