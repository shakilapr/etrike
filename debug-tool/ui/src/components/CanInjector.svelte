<script lang="ts">
  import { commandAcks } from "../stores/can";
  import { sendFrame, startPeriodic, stopPeriodic } from "../lib/api";
  import type { Bus, CanField, CanMessageDef, InjectionTemplate } from "../lib/can-decoder";
  import { BUSES, encodePayload, formatBytes } from "../lib/can-decoder";

  export let ids: CanMessageDef[] = [];
  export let templates: InjectionTemplate[] = [];

  let selectedBus: Bus = "high";
  let selectedId = "0x300";
  let values: Record<string, number | boolean> = {
    speed_mmps: 2000,
    yaw_rate_mrad_s: 0,
    gear: 1
  };
  let confirmEstop = false;
  let intervalMs = 20;
  let count = 500;
  let error = "";
  let pending = false;

  $: busIds = ids.filter((item) => item.bus === selectedBus);
  $: injectableIds = busIds.filter((item) => item.injectable);
  $: selected = injectableIds.find((item) => item.id === selectedId) ?? injectableIds[0];
  $: encoded = selected ? encodePayload(selectedBus, selected.id, values) : { dlc: 0, data: [] };

  function chooseBus(bus: Bus) {
    selectedBus = bus;
    const first = ids.find((item) => item.bus === bus && item.injectable);
    if (first) chooseId(first.id);
  }

  function chooseId(id: string) {
    selectedId = id;
    const next = injectableIds.find((item) => item.id === id);
    values = defaultsFor(next);
    confirmEstop = false;
  }

  function applyTemplate(template: InjectionTemplate) {
    selectedBus = template.bus;
    selectedId = template.id;
    values = { ...template.values };
    confirmEstop = false;
  }

  function updateField(field: CanField, value: string | boolean) {
    values = {
      ...values,
      [field.key]: field.kind === "boolean" ? Boolean(value) : Number(value)
    };
  }

  async function sendOnce() {
    await command(() =>
      sendFrame({
        bus: selectedBus,
        id: selected.id,
        dlc: encoded.dlc,
        data: encoded.data,
        confirm_estop: confirmEstop
      })
    );
  }

  async function startLoop() {
    await command(() =>
      startPeriodic({
        bus: selectedBus,
        id: selected.id,
        dlc: encoded.dlc,
        data: encoded.data,
        interval_ms: intervalMs,
        count,
        confirm_estop: confirmEstop
      })
    );
  }

  async function stopLoop() {
    await command(() => stopPeriodic(selectedBus, selected.id));
  }

  async function command(run: () => Promise<unknown>) {
    pending = true;
    error = "";
    try {
      await run();
    } catch (err) {
      error = err instanceof Error ? err.message : String(err);
    } finally {
      pending = false;
    }
  }

  function defaultsFor(message?: CanMessageDef): Record<string, number | boolean> {
    const next: Record<string, number | boolean> = {};
    for (const field of message?.fields ?? []) {
      if (field.kind === "boolean") next[field.key] = false;
      else if (field.options?.length) next[field.key] = field.options[0].value;
      else next[field.key] = field.min && field.min > 0 ? field.min : 0;
    }
    if (message?.id === "0x300") {
      next.speed_mmps = 2000;
      next.yaw_rate_mrad_s = 0;
      next.gear = 1;
    }
    if (message?.id === "0x400") {
      next.distance_mm = 4294967295;
    }
    return next;
  }
</script>

<section class="injector-layout">
  <div class="panel">
    <div class="panel-title">
      <h2>CAN Injector</h2>
      <span class="mono">{formatBytes(encoded.data)}</span>
    </div>

    <div class="bus-tabs">
      {#each BUSES as bus}
        <button class:active={selectedBus === bus} type="button" on:click={() => chooseBus(bus)}>
          {bus.toUpperCase()} Bus
        </button>
      {/each}
    </div>

    <label class="field">
      <span>CAN ID</span>
      <select bind:value={selectedId} on:change={(event) => chooseId(event.currentTarget.value)}>
        {#each injectableIds as item}
          <option value={item.id}>{item.id} {item.name}</option>
        {/each}
      </select>
    </label>

    {#if selected}
      <div class="form-grid">
        {#each selected.fields as field}
          <label class="field">
            <span>{field.label}{field.unit ? ` (${field.unit})` : ""}</span>
            {#if field.kind === "boolean"}
              <input
                checked={Boolean(values[field.key])}
                type="checkbox"
                on:change={(event) => updateField(field, event.currentTarget.checked)}
              />
            {:else if field.kind === "enum"}
              <select value={String(values[field.key] ?? field.options?.[0]?.value ?? 0)} on:change={(event) => updateField(field, event.currentTarget.value)}>
                {#each field.options ?? [] as option}
                  <option value={option.value}>{option.label}</option>
                {/each}
              </select>
            {:else}
              <input
                max={field.max}
                min={field.min}
                step={field.step ?? 1}
                type="number"
                value={String(values[field.key] ?? 0)}
                on:input={(event) => updateField(field, event.currentTarget.value)}
              />
            {/if}
          </label>
        {/each}
      </div>
    {/if}

    {#if selected?.id === "0x001"}
      <label class="confirm-row">
        <input bind:checked={confirmEstop} type="checkbox" />
        <span>Confirm ESTOP injection</span>
      </label>
    {/if}

    <div class="periodic-controls">
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
      <button disabled={pending || !selected} type="button" on:click={sendOnce}>Send Once</button>
      <button disabled={pending || !selected} type="button" on:click={startLoop}>Start Periodic</button>
      <button disabled={pending || !selected} type="button" on:click={stopLoop}>Stop</button>
    </div>
  </div>

  <div class="panel">
    <div class="panel-title">
      <h2>Templates</h2>
      <span>{templates.length}</span>
    </div>
    <div class="template-list">
      {#each templates as template}
        <button type="button" on:click={() => applyTemplate(template)}>
          <strong class="mono">{template.bus}:{template.id}</strong>
          <span>{template.name} — {template.description}</span>
        </button>
      {/each}
    </div>
  </div>

  <div class="panel history-panel">
    <div class="panel-title">
      <h2>Command Acks</h2>
      <span>{$commandAcks.length}</span>
    </div>
    <div class="ack-list">
      {#each $commandAcks as ack}
        <pre>{JSON.stringify(ack, null, 2)}</pre>
      {/each}
    </div>
  </div>
</section>
