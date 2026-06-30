<script lang="ts">
  import type { CanMessageIndex } from "../lib/can-index";

  export let message: CanMessageIndex;
  export let activeSignal = -1;

  const COLORS = [
    "#4ea1ff", "#e0556a", "#4caf82", "#e6b34a", "#b06bff", "#3dd6c8",
    "#ee5e5e", "#7bc96f", "#8f7cff", "#2fb6a7", "#e09f3e", "#6f91ff"
  ];

  $: bitMap = buildBitMap(message);
  $: wide = message.dlc >= 7;

  function buildBitMap(input: CanMessageIndex): number[] {
    const map = Array.from({ length: Math.max(input.dlc, 0) * 8 }, () => -1);
    input.signals.forEach((signal, index) => {
      const start = signal.byte * 8 + signal.bit_offset;
      for (let offset = 0; offset < signal.size; offset++) {
        const bit = start + offset;
        if (bit >= 0 && bit < map.length) map[bit] = index;
      }
    });
    return map;
  }

  function colorFor(index: number): string {
    return COLORS[index % COLORS.length];
  }
</script>

{#if message.dlc === 0}
  <div class="bit-empty">DLC=0 event frame. The CAN ID is the signal.</div>
{:else}
  <div class="byte-grid-scroll" aria-label={`${message.name} byte layout`}>
    <div class="byte-grid">
      {#each Array.from({ length: message.dlc }) as _, byte}
        <div class="byte-col">
          <span class="byte-label">B{byte}</span>
          <div class="bit-row">
            {#each [7, 6, 5, 4, 3, 2, 1, 0] as bit}
              {@const signalIndex = bitMap[byte * 8 + bit]}
              <button
                class="bit-cell"
                class:wide
                class:filled={signalIndex >= 0}
                class:highlight={activeSignal === signalIndex && signalIndex >= 0}
                style={signalIndex >= 0 ? `--bit-color:${colorFor(signalIndex)}` : ""}
                title={signalIndex >= 0 ? message.signals[signalIndex].name : `B${byte}.${bit}`}
                type="button"
                on:mouseenter={() => (activeSignal = signalIndex)}
                on:mouseleave={() => (activeSignal = -1)}
                on:focus={() => (activeSignal = signalIndex)}
                on:blur={() => (activeSignal = -1)}
              >
                <span>{bit}</span>
              </button>
            {/each}
          </div>
        </div>
      {/each}
    </div>
  </div>
{/if}

<style>
  .byte-grid-scroll {
    overflow-x: auto;
    padding: 4px 0 2px;
  }

  .byte-grid {
    display: flex;
    gap: 8px;
    min-width: fit-content;
  }

  .byte-col {
    display: grid;
    gap: 4px;
    justify-items: center;
  }

  .byte-label {
    color: var(--muted);
    font-family: "Cascadia Mono", "SFMono-Regular", Consolas, monospace;
    font-size: 0.66rem;
    font-weight: 700;
  }

  .bit-row {
    display: flex;
    gap: 2px;
  }

  .bit-cell {
    align-items: center;
    background: var(--bg);
    border: 1px solid var(--panel-border);
    border-radius: 3px;
    color: var(--muted);
    display: inline-flex;
    height: 18px;
    justify-content: center;
    min-height: 18px;
    padding: 0;
    width: 18px;
  }

  .bit-cell.wide {
    height: 16px;
    min-height: 16px;
    width: 16px;
  }

  .bit-cell span {
    font-family: "Cascadia Mono", "SFMono-Regular", Consolas, monospace;
    font-size: 0.52rem;
    line-height: 1;
  }

  .bit-cell.filled {
    background: color-mix(in srgb, var(--bit-color) 78%, var(--bg));
    border-color: var(--bit-color);
    color: #ffffff;
  }

  .bit-cell.highlight,
  .bit-cell:hover,
  .bit-cell:focus-visible {
    outline: 2px solid var(--fg);
    outline-offset: 1px;
    transform: scale(1.2);
    z-index: 2;
  }

  .bit-empty {
    background: var(--bg);
    border: 1px dashed var(--panel-border);
    border-radius: 6px;
    color: var(--muted);
    font-size: 0.82rem;
    padding: 12px;
  }
</style>
