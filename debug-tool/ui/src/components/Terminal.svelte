<script lang="ts">
  import { errorLog, clearErrors, type ErrorEntry } from "../stores/errors";

  let copiedIdx: number | null = null;
  let copiedAll = false;

  function fmtTime(ts: number): string {
    const d = new Date(ts * 1000);
    const h = String(d.getHours()).padStart(2, "0");
    const m = String(d.getMinutes()).padStart(2, "0");
    const s = String(d.getSeconds()).padStart(2, "0");
    const ms = String(d.getMilliseconds()).padStart(3, "0");
    return h + ":" + m + ":" + s + "." + ms;
  }

  async function copyEntry(entry: ErrorEntry, idx: number) {
    const line = "[" + fmtTime(entry.ts) + "] " + entry.message;
    try {
      await navigator.clipboard.writeText(line);
      copiedIdx = idx;
      setTimeout(() => (copiedIdx = null), 1500);
    } catch {
      // fallback for insecure contexts
      const ta = document.createElement("textarea");
      ta.value = line;
      ta.style.position = "fixed";
      ta.style.opacity = "0";
      document.body.appendChild(ta);
      ta.select();
      document.execCommand("copy");
      document.body.removeChild(ta);
      copiedIdx = idx;
      setTimeout(() => (copiedIdx = null), 1500);
    }
  }

  async function copyAll() {
    const text = $errorLog
      .map((e) => "[" + fmtTime(e.ts) + "] " + e.message)
      .join("\n");
    try {
      await navigator.clipboard.writeText(text);
    } catch {
      const ta = document.createElement("textarea");
      ta.value = text;
      ta.style.position = "fixed";
      ta.style.opacity = "0";
      document.body.appendChild(ta);
      ta.select();
      document.execCommand("copy");
      document.body.removeChild(ta);
    }
    copiedAll = true;
    setTimeout(() => (copiedAll = false), 1500);
  }
</script>

<div class="terminal-panel">
  <div class="terminal-header">
    <span class="terminal-title">Errors ({$errorLog.length})</span>
    <div class="terminal-actions">
      {#if $errorLog.length > 0}
        <button class="term-btn" on:click={copyAll} title="Copy all errors">
          {copiedAll ? "✓ Copied" : "📋 Copy all"}
        </button>
        <button class="term-btn danger" on:click={clearErrors} title="Clear error log">
          ✕ Clear
        </button>
      {/if}
    </div>
  </div>
  <div class="terminal-body" class:empty={$errorLog.length === 0}>
    {#if $errorLog.length === 0}
      <span class="terminal-empty">No errors. All clear.</span>
    {:else}
      {#each $errorLog as entry, i (entry.ts + "_" + i)}
        <div class="terminal-line" class:error={entry.message.toLowerCase().includes("error")} class:warn={!entry.message.toLowerCase().includes("error")}>
          <span class="term-time">[{fmtTime(entry.ts)}]</span>
          <span class="term-msg">{entry.message}</span>
          <button
            class="term-copy"
            on:click={() => copyEntry(entry, i)}
            title="Copy this error"
          >
            {copiedIdx === i ? "✓" : "📋"}
          </button>
        </div>
      {/each}
    {/if}
  </div>
</div>
