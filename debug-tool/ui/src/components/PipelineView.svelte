<script lang="ts">
  import { onMount } from "svelte";
  import { formatDecoded, frameTime } from "../lib/can-decoder";

  interface PipelineNode {
    bus: string;
    id: string;
    name: string;
    decoded: Record<string, unknown>;
    ts: number;
  }

  interface PipelineChain {
    trigger: PipelineNode;
    steps: PipelineNode[];
  }

  let chains: PipelineChain[] = [];
  let error = "";
  let pollTimer: ReturnType<typeof setInterval>;

  async function refresh() {
    try {
      const resp = await fetch("/api/can/pipeline");
      const data = await resp.json();
      chains = data.chains ?? [];
      error = "";
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    }
  }

  onMount(() => {
    void refresh();
    pollTimer = setInterval(refresh, 2000);
    return () => clearInterval(pollTimer);
  });

  function latencyMs(from: number, to: number): string {
    return `${Math.round((to - from) * 1000)}ms`;
  }

  function nodeLabel(node: PipelineNode): string {
    const entries = Object.entries(node.decoded)
      .filter(([k]) => !k.endsWith("_name") && !k.endsWith("_label") && !k.endsWith("_hex"))
      .slice(0, 2);
    return entries.map(([k, v]) => `${k.replace(/_/g, " ")}=${v}`).join(", ");
  }
</script>

<section class="pipeline-view">
  <div class="panel-title">
    <h2>Pipeline — Bus-to-Bus Correlation</h2>
    <span>{chains.length} chains</span>
  </div>

  {#if error}
    <div class="alert">{error}</div>
  {/if}

  {#if chains.length === 0}
    <div class="cat-empty">No correlated chains yet. Inject 0x300 on the high bus to see the pipeline.</div>
  {:else}
    <div class="pipeline-scroll">
      {#each chains as chain, ci (`chain-${ci}-${chain.trigger.ts}`)}
        <div class="pipeline-chain">
          <!-- Trigger node -->
          <div class="pipe-node high">
            <div class="pipe-bus">HIGH</div>
            <div class="pipe-id">{chain.trigger.id}</div>
            <div class="pipe-name">{chain.trigger.name}</div>
            <div class="pipe-val">{nodeLabel(chain.trigger)}</div>
            <div class="pipe-ts">{frameTime({ ts: 0, bus: "high", id: "", name: "", dlc: 0, data: [], decoded: {}, ts_real: chain.trigger.ts })}</div>
          </div>

          {#each chain.steps as step, si (`step-${ci}-${si}`)}
            {@const prev = si === 0 ? chain.trigger : chain.steps[si - 1]}
            <div class="pipe-arrow">
              <span class="pipe-latency">{latencyMs(prev.ts, step.ts)}</span>
              <span class="pipe-arrow-line">→</span>
            </div>
            <div class="pipe-node low">
              <div class="pipe-bus">LOW</div>
              <div class="pipe-id">{step.id}</div>
              <div class="pipe-name">{step.name}</div>
              <div class="pipe-val">{nodeLabel(step)}</div>
              <div class="pipe-ts">{frameTime({ ts: 0, bus: "low", id: "", name: "", dlc: 0, data: [], decoded: {}, ts_real: step.ts })}</div>
            </div>
          {/each}
        </div>
        {#if ci < chains.length - 1}
          <div class="pipe-divider"></div>
        {/if}
      {/each}
    </div>
  {/if}
</section>
