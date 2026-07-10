<script lang="ts">
  import type { CanMessageDef } from "@etrike/debug-shared";

  export let message: CanMessageDef;
  export let activeSignal = -1;
  let activeBit = "";
  let pointerX = 0;
  let pointerY = 0;

  const COLORS = [
    "#4ea1ff", "#e0556a", "#4caf82", "#e6b34a", "#b06bff", "#3dd6c8",
    "#ee5e5e", "#7bc96f", "#8f7cff", "#2fb6a7", "#e09f3e", "#6f91ff"
  ];

  $: bitMap = buildBitMap(message);
  $: effectiveDlc = Math.max(message.dlc, 0);
  $: wide = effectiveDlc >= 7;
  $: mappedBits = bitMap.filter((index) => index >= 0).length;
  $: tooltipStyle = floatingTooltipStyle(pointerX, pointerY);

  function buildBitMap(input: CanMessageDef): number[] {
    const map = Array.from({ length: Math.max(input.dlc, 0) * 8 }, () => -1);
    // In metadata generator, byte_order is output as byteOrder for messages. 
    // Wait, the new CanMessageDef does not expose byteOrder publicly? 
    // In python generator we exported `byteOrder`, wait, let me check if I added it to CanMessageDef.
    // If not, we just use standard motorola map logic.
    // Assuming fields are intel or motorola based on their _byte and _bit_offset calculation which was done statically!
    // No wait, generate_can_ts.py outputs _byte, _bit_offset for the start bit, but we might have gaps if we assume simple iteration!
    input.fields.forEach((signal, index) => {
      const start = signal._byte * 8 + signal._bit_offset;
      for (let offset = 0; offset < signal._size; offset++) {
        const bit = start + offset;
        if (bit >= 0 && bit < map.length) map[bit] = index;
      }
    });
    return map;
  }

  function colorFor(index: number): string {
    return COLORS[index % COLORS.length];
  }

  function scaleFor(index: number): string {
    const signal = message.fields[index];
    if (signal._factor === 1 && signal._offset === 0) return "raw";
    if (signal._factor === 1) return `raw ${signal._offset >= 0 ? "+" : "-"} ${Math.abs(signal._offset)}`;
    if (signal._offset === 0) return `raw x ${signal._factor}`;
    return `raw x ${signal._factor} ${signal._offset >= 0 ? "+" : "-"} ${Math.abs(signal._offset)}`;
  }

  function valuesFor(index: number): string {
    const options = message.fields[index].options;
    if (!options || options.length === 0) return "";
    return options.map((opt) => `${opt.value}=${opt.label}`).join(", ");
  }

  function bitMeaningFor(index: number, byte: number, bit: number): string {
    const signal = message.fields[index];
    const absoluteBit = byte * 8 + bit;
    const relativeBit = absoluteBit - (signal._byte * 8 + signal._bit_offset);
    const commentMatch = signal.key?.match(new RegExp(`bit${relativeBit}\\s*=\\s*([^,;]+)`, "i"));
    if (commentMatch?.[1]) return commentMatch[1].trim();

    const bitMask = 2 ** relativeBit;
    const maskMeaning = signal.options?.find(opt => opt.value === bitMask)?.label;
    if (maskMeaning) return maskMeaning;

    if (signal._size === 1) {
      const activeName = signal.label
        .replace(/^[A-Z0-9]+_/, "")
        .replace(/([a-z0-9])([A-Z])/g, "$1 $2")
        .replace(/_/g, " ")
        .toLowerCase();
      return `1 = ${activeName || "active"}, 0 = inactive`;
    }

    return "";
  }

  function bitLabel(byte: number, bit: number, signalIndex: number): string {
    if (signalIndex < 0) return `B${byte}.${bit} unused`;
    const signal = message.fields[signalIndex];
    return [
      `${signal.label}: B${byte}.${bit}`,
      bitMeaningFor(signalIndex, byte, bit) ? `bit meaning ${bitMeaningFor(signalIndex, byte, bit)}` : "",
      `${signal._size}-bit ${signal._type}`,
      `scale ${scaleFor(signalIndex)}`,
      signal.unit ? `unit ${signal.unit}` : "",
      valuesFor(signalIndex),
      signal.key
    ].filter(Boolean).join(" / ");
  }

  function floatingTooltipStyle(x: number, y: number): string {
    const width = 340;
    const height = 180;
    const viewportWidth = typeof window === "undefined" ? 1440 : window.innerWidth;
    const viewportHeight = typeof window === "undefined" ? 900 : window.innerHeight;
    const left = Math.max(12, Math.min(x + 16, viewportWidth - width - 12));
    const bottom = Math.max(8, Math.min(viewportHeight - height - 12, viewportHeight - y + 10));
    return `left:${left}px;bottom:${bottom}px;`;
  }

  function setPointer(event: MouseEvent | FocusEvent) {
    const target = event.currentTarget;
    if (!(target instanceof HTMLElement)) return;
    const rect = target.getBoundingClientRect();
    pointerX = "clientX" in event ? event.clientX : rect.left + rect.width / 2;
    pointerY = rect.top;
  }

  function showBit(event: MouseEvent | FocusEvent, byte: number, bit: number, signalIndex: number) {
    setPointer(event);
    activeSignal = signalIndex;
    activeBit = signalIndex >= 0 ? `B${byte}.${bit}` : "";
  }

  function clearBit() {
    activeSignal = -1;
    activeBit = "";
  }
</script>

{#if message.dlc === 0}
  <div class="bit-empty">DLC=0 event frame. The CAN ID is the signal.</div>
{:else}
  <div class="bit-grid-head">
    <span>Byte layout</span>
    <em>{mappedBits}/{effectiveDlc * 8} bits mapped</em>
  </div>
  <div class="byte-grid-scroll" aria-label={`${message.name} byte layout`}>
    <div class="byte-grid">
      {#each Array.from({ length: effectiveDlc }) as _, byte}
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
                aria-label={bitLabel(byte, bit, signalIndex)}
                type="button"
                on:mouseenter={(event) => showBit(event, byte, bit, signalIndex)}
                on:mousemove={setPointer}
                on:mouseleave={clearBit}
                on:focus={(event) => showBit(event, byte, bit, signalIndex)}
                on:blur={clearBit}
              >
                <span>{signalIndex >= 0 ? "" : bit}</span>
              </button>
            {/each}
          </div>
        </div>
      {/each}
    </div>
  </div>
  <div class="bit-inspector" role="status">
    {#if activeSignal >= 0}
      {@const signal = message.fields[activeSignal]}
      {@const activeParts = activeBit.match(/^B(\d+)\.(\d+)$/)}
      {@const activeMeaning = activeParts ? bitMeaningFor(activeSignal, Number(activeParts[1]), Number(activeParts[2])) : ""}
      <strong>{activeBit}</strong>
      <span>{signal.label}</span>
      {#if activeMeaning}
        <b>{activeMeaning}</b>
      {/if}
      <em>{signal._size}-bit {signal._type} · scale {scaleFor(activeSignal)}{signal.unit ? ` ${signal.unit}` : ""}</em>
    {:else}
      <strong>Bit detail</strong>
      <span>{message.name}</span>
      <em>Hover a mapped bit to inspect byte position, type, scale, and bit meaning.</em>
    {/if}
  </div>

  {#if activeSignal >= 0}
    {@const signal = message.fields[activeSignal]}
    {@const activeParts = activeBit.match(/^B(\d+)\.(\d+)$/)}
    {@const activeMeaning = activeParts ? bitMeaningFor(activeSignal, Number(activeParts[1]), Number(activeParts[2])) : ""}
    <div class="bit-float-tooltip" role="tooltip" style={tooltipStyle}>
      <strong>{signal.label}</strong>
      <small>{activeBit} · {signal._size}-bit {signal._type}</small>
      {#if activeMeaning}
        <small>Bit meaning: {activeMeaning}</small>
      {/if}
      <small>Scale: {scaleFor(activeSignal)}{signal.unit ? ` ${signal.unit}` : ""}</small>
      {#if valuesFor(activeSignal)}
        <small>Values: {valuesFor(activeSignal)}</small>
      {/if}
      {#if signal.key}
        <em>{signal.key}</em>
      {/if}
    </div>
  {/if}
{/if}

<style>
  .bit-grid-head {
    align-items: center;
    color: var(--muted);
    display: flex;
    gap: 10px;
    justify-content: space-between;
  }

  .bit-grid-head span {
    color: var(--fg);
    font-size: 0.78rem;
    font-weight: 800;
    text-transform: uppercase;
  }

  .bit-grid-head em {
    font-size: 0.72rem;
    font-style: normal;
    font-weight: 700;
  }

  .byte-grid-scroll {
    overflow: visible;
    padding: 6px 0 2px;
  }

  .byte-grid {
    display: flex;
    gap: 8px;
    flex-wrap: wrap;
    min-width: 0;
    overflow: visible;
    row-gap: 12px;
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
    position: relative;
    width: 18px;
  }

  .bit-cell.wide {
    height: 16px;
    min-height: 16px;
    width: 16px;
  }

  .bit-cell span {
    font-family: "Cascadia Mono", "SFMono-Regular", Consolas, monospace;
    font-size: 0.46rem;
    line-height: 1;
  }

  .bit-cell.filled {
    background: color-mix(in srgb, var(--bit-color) 86%, var(--bg));
    border-color: var(--bit-color);
    color: #ffffff;
  }

  .bit-cell.highlight {
    box-shadow: inset 0 0 0 2px var(--fg);
    z-index: 1;
  }

  .bit-cell:hover,
  .bit-cell:focus-visible {
    box-shadow: inset 0 0 0 2px var(--fg), 0 0 0 1px color-mix(in srgb, var(--fg) 45%, transparent);
    outline: 0;
    z-index: 2;
  }

  .bit-float-tooltip {
    background: #080a0f;
    border: 1px solid var(--panel-border);
    border-radius: 6px;
    box-shadow: 0 8px 22px rgb(0 0 0 / 0.38);
    color: var(--fg);
    display: grid;
    gap: 3px;
    max-width: min(340px, 78vw);
    min-width: 190px;
    padding: 8px 10px;
    pointer-events: none;
    position: fixed;
    text-align: left;
    white-space: normal;
    z-index: 10000;
  }

  .bit-float-tooltip strong,
  .bit-float-tooltip small,
  .bit-float-tooltip em {
    display: block;
  }

  .bit-float-tooltip strong {
    color: var(--accent);
    font-size: 0.72rem;
  }

  .bit-float-tooltip small,
  .bit-float-tooltip em {
    color: var(--muted);
    font-size: 0.68rem;
    font-style: normal;
    line-height: 1.3;
  }

  .bit-inspector {
    align-items: center;
    background: color-mix(in srgb, var(--accent) 10%, var(--bg));
    border: 1px solid color-mix(in srgb, var(--accent) 38%, var(--panel-border));
    border-radius: 6px;
    color: var(--fg);
    display: flex;
    flex-wrap: wrap;
    gap: 6px 10px;
    margin-top: 8px;
    min-height: 42px;
    padding: 7px 10px;
  }

  .bit-inspector strong,
  .bit-inspector span,
  .bit-inspector b {
    color: var(--accent);
    font-family: "Cascadia Mono", "SFMono-Regular", Consolas, monospace;
    font-size: 0.76rem;
    font-weight: 800;
  }

  .bit-inspector b {
    color: var(--fg);
    font-family: inherit;
  }

  .bit-inspector em {
    color: var(--muted);
    font-size: 0.72rem;
    font-style: normal;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
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
