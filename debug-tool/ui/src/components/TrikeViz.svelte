<script lang="ts">
  import { onMount, onDestroy } from "svelte";
  import { telemetry } from "../stores/telemetry";

  export let visible = false;

  let x = 0, y = 0, theta = 0, v = 0, alpha = 0;
  const L = 120, W = 80, MAX_A = Math.PI / 3, MAX_V = 300;
  const VIEW_ROTATION = -Math.PI / 2;
  let canvas: HTMLCanvasElement;
  let ctx: CanvasRenderingContext2D;
  let animId = 0;
  let lastTime = performance.now();
  let w = 320, h = 400;
  let speedKmh: number | null = null;
  let headingDeg = 0;
  let steerDeg: number | null = null;
  let turnRadius = "No data";
  let gear: string | null = null;
  let mode: string | null = null;
  let safetyState: string | null = null;
  let unsub: () => void;
  let observer: ResizeObserver;

  // Toggle animation based on visibility
  $: if (visible && !animId) {
    lastTime = performance.now();
    animId = requestAnimationFrame(loop);
  } else if (!visible && animId) {
    cancelAnimationFrame(animId);
    animId = 0;
  }

  onMount(() => {
    ctx = canvas.getContext("2d")!;
    resize();
    window.addEventListener("resize", resize);
    observer = new ResizeObserver(() => resize());
    if (canvas.parentElement) observer.observe(canvas.parentElement);

    unsub = telemetry.subscribe((t) => {
      speedKmh = t.motorSpeedKmh;
      steerDeg = t.steerAngleDeg;
      gear = t.gear;
      mode = t.mode;
      safetyState = t.safetyState;

      const signedSpeedKmh = speedKmh === null ? 0 : gear === "R" ? -Math.abs(speedKmh) : speedKmh;
      v = signedSpeedKmh * 1000 / 3.6 * 0.25;
      alpha = steerDeg === null ? 0 : Math.max(-MAX_A, Math.min(MAX_A, steerDeg * Math.PI / 180));
      syncReadouts();
    });

    if (visible) {
      lastTime = performance.now();
      animId = requestAnimationFrame(loop);
    }
  });

  onDestroy(() => {
    if (animId) cancelAnimationFrame(animId);
    window.removeEventListener("resize", resize);
    observer?.disconnect();
    unsub?.();
  });

  function resize() {
    if (!canvas) return;
    const rect = canvas.parentElement?.getBoundingClientRect();
    w = rect?.width ?? 320; h = rect?.height ?? 400;
    canvas.width = w * devicePixelRatio;
    canvas.height = h * devicePixelRatio;
    canvas.style.width = w + "px";
    canvas.style.height = h + "px";
    ctx = canvas.getContext("2d")!;
    ctx.scale(devicePixelRatio, devicePixelRatio);
  }

  function update(dt: number) {
    if (dt > 0.1) return;
    const td = (v / L) * Math.tan(alpha);
    theta += td * dt;
    if (theta > Math.PI * 2) theta -= Math.PI * 2;
    if (theta < -Math.PI * 2) theta += Math.PI * 2;
    const visualTheta = theta + VIEW_ROTATION;
    x += v * Math.cos(visualTheta) * dt;
    y += v * Math.sin(visualTheta) * dt;
    syncReadouts();
  }

  function syncReadouts() {
    headingDeg = (theta * 180 / Math.PI) % 360;
    if (headingDeg > 180) headingDeg -= 360;
    if (headingDeg < -180) headingDeg += 360;
    turnRadius = steerDeg === null ? "No data" : Math.abs(alpha) < 0.005 ? "Straight (∞)" : Math.abs(L / Math.tan(alpha)).toFixed(0) + " px";
  }

  function drawGrid(cx: number, cy: number) {
    const gs = 40;
    const ox = ((-x % gs) + gs) % gs;
    const oy = ((-y % gs) + gs) % gs;

    ctx.strokeStyle = "rgba(124,130,152,0.28)";
    ctx.lineWidth = 0.5;
    ctx.beginPath();
    for (let gx = ox - gs; gx < w; gx += gs) { ctx.moveTo(gx, 0); ctx.lineTo(gx, h); }
    for (let gy = oy - gs; gy < h; gy += gs) { ctx.moveTo(0, gy); ctx.lineTo(w, gy); }
    ctx.stroke();

    const originX = cx - x;
    const originY = cy - y;
    ctx.strokeStyle = "rgba(225,228,234,0.18)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(originX - 1000, originY); ctx.lineTo(originX + 1000, originY);
    ctx.moveTo(originX, originY - 1000); ctx.lineTo(originX, originY + 1000);
    ctx.stroke();

    ctx.fillStyle = "rgba(225,228,234,0.38)";
    ctx.font = "10px Cascadia Mono, Consolas, monospace";
    ctx.fillText("Global origin", originX + 6, originY - 6);
  }

  function drawVehicle(cx: number, cy: number) {
    const heading = theta + VIEW_ROTATION;

    ctx.save();
    ctx.translate(cx, cy);

    ctx.strokeStyle = "rgba(225,228,234,0.32)";
    ctx.setLineDash([5, 5]);
    ctx.beginPath();
    ctx.moveTo(0, 0);
    ctx.lineTo(0, -145);
    ctx.stroke();
    ctx.setLineDash([]);

    ctx.rotate(heading);

    if (Math.abs(alpha) > 0.01) {
      const R = L / Math.tan(alpha);
      ctx.strokeStyle = "rgba(176,107,255,0.48)";
      ctx.setLineDash([8, 4]);
      ctx.beginPath();
      ctx.moveTo(0, 0); ctx.lineTo(0, R);
      ctx.moveTo(L, 0); ctx.lineTo(0, R);
      ctx.stroke();
      ctx.setLineDash([]);
      ctx.fillStyle = "#b06bff";
      ctx.beginPath(); ctx.arc(0, R, 4, 0, Math.PI * 2); ctx.fill();
      ctx.font = "11px Cascadia Mono, Consolas, monospace";
      ctx.fillText("C (ICR)", 9, R - 7);
    }

    const cW = L + 44;
    const cH = W + 44;
    ctx.fillStyle = "rgba(22,24,34,0.86)";
    ctx.strokeStyle = "#7c8298";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.roundRect(-30, -cH / 2, cW, cH, 14);
    ctx.fill();
    ctx.stroke();

    ctx.strokeStyle = "rgba(124,130,152,0.8)";
    ctx.setLineDash([4, 4]);
    ctx.beginPath(); ctx.moveTo(-40, 0); ctx.lineTo(L + 60, 0); ctx.stroke();
    ctx.setLineDash([]);

    ctx.strokeStyle = "#7c8298";
    ctx.beginPath(); ctx.moveTo(0, -W / 2); ctx.lineTo(0, W / 2); ctx.stroke();

    const wLen = 34;
    const wThick = 14;
    ctx.fillStyle = "#4caf82";
    ctx.strokeStyle = "#0f1117";
    ctx.fillRect(-wLen / 2, -W / 2 - wThick / 2, wLen, wThick);
    ctx.strokeRect(-wLen / 2, -W / 2 - wThick / 2, wLen, wThick);
    ctx.fillRect(-wLen / 2, W / 2 - wThick / 2, wLen, wThick);
    ctx.strokeRect(-wLen / 2, W / 2 - wThick / 2, wLen, wThick);

    ctx.save();
    ctx.translate(L, 0);

    ctx.strokeStyle = "rgba(225,228,234,0.32)";
    ctx.setLineDash([5, 5]);
    ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(75, 0); ctx.stroke();
    ctx.setLineDash([]);

    ctx.rotate(alpha);
    ctx.fillStyle = "#e0556a";
    ctx.strokeStyle = "#0f1117";
    ctx.fillRect(-wLen / 2, -wThick / 2, wLen, wThick);
    ctx.strokeRect(-wLen / 2, -wThick / 2, wLen, wThick);

    if (Math.abs(v) > 2) {
      const vl = Math.max(-70, Math.min(70, (v / MAX_V) * 60));
      const arrow = vl >= 0 ? 1 : -1;
      ctx.strokeStyle = "#b06bff";
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(0, 0);
      ctx.lineTo(vl, 0);
      ctx.lineTo(vl - arrow * 7, -4);
      ctx.moveTo(vl, 0);
      ctx.lineTo(vl - arrow * 7, 4);
      ctx.stroke();
      ctx.fillStyle = "#b06bff";
      ctx.font = "11px Cascadia Mono, Consolas, monospace";
      ctx.fillText("Vs", vl + arrow * 6, 5);
    }
    ctx.restore();

    if (Math.abs(alpha) > 0.02) {
      ctx.save();
      ctx.translate(L, 0);
      ctx.strokeStyle = "#e0556a";
      ctx.lineWidth = 2;
      ctx.beginPath(); ctx.arc(0, 0, 48, Math.min(0, alpha), Math.max(0, alpha)); ctx.stroke();
      ctx.fillStyle = "#e0556a";
      ctx.font = "16px Georgia, serif";
      ctx.fillText("α", 58 * Math.cos(alpha / 2), 58 * Math.sin(alpha / 2) + 5);
      ctx.restore();
    }

    ctx.fillStyle = "#e1e4ea";
    ctx.strokeStyle = "rgba(225,228,234,0.7)";
    ctx.lineWidth = 1;
    ctx.font = "12px Inter, sans-serif";
    ctx.fillText("L", L / 2 - 4, -10);
    ctx.beginPath();
    ctx.moveTo(0, -5); ctx.lineTo(0, 5);
    ctx.moveTo(L, -5); ctx.lineTo(L, 5);
    ctx.moveTo(0, 0); ctx.lineTo(L, 0);
    ctx.stroke();

    ctx.restore();

    if (Math.abs(theta) > 0.01) {
      ctx.strokeStyle = "#4ea1ff";
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.arc(cx, cy, 58, VIEW_ROTATION, VIEW_ROTATION + theta, theta < 0);
      ctx.stroke();
      ctx.fillStyle = "#4ea1ff";
      ctx.font = "16px Georgia, serif";
      ctx.fillText("θ", cx + 68 * Math.cos(VIEW_ROTATION + theta / 2), cy + 68 * Math.sin(VIEW_ROTATION + theta / 2) + 5);
    }
  }

  function draw() {
    const cx = w / 2;
    const cy = h > 520 ? h * 0.58 : h * 0.62;
    ctx.clearRect(0, 0, w, h);
    ctx.fillStyle = "#0f1117";
    ctx.fillRect(0, 0, w, h);
    drawGrid(cx, cy);
    drawVehicle(cx, cy);
  }

  function loop(ts: number) {
    const dt = (ts - lastTime) / 1000;
    lastTime = ts;
    if (dt < 0.2) update(dt);
    draw();
    if (visible) animId = requestAnimationFrame(loop);
  }
</script>

<div class="trike-viz">
  <div class="trike-hud">
    <section class="hud-card hud-main">
      <p class="hud-kicker">Actual Vehicle State</p>
      <h2>Tricycle Kinematics</h2>
      <p class="hud-subtitle">Live CAN telemetry · passive ego-view</p>
      <div class="hud-grid">
        <span>Velocity (v)</span><strong>{speedKmh !== null ? speedKmh.toFixed(1) + " km/h" : "No data"}</strong>
        <span>Heading (θ)</span><strong>{headingDeg.toFixed(1)}°</strong>
        <span>Steering (α)</span><strong>{steerDeg !== null ? steerDeg.toFixed(1) + "°" : "No data"}</strong>
        <span>Turn radius (ρ)</span><strong>{turnRadius}</strong>
        <span>Gear / Mode</span><strong>{gear ?? "--"} / {mode ?? "No mode"}</strong>
        <span>Safety</span><strong>{safetyState ?? "No data"}</strong>
      </div>
    </section>
  </div>
  <canvas bind:this={canvas}></canvas>
</div>

<style>
  .trike-viz {
    background: #0f1117;
    border: 1px solid var(--panel-border);
    border-radius: 6px;
    height: 100%;
    min-height: 300px;
    overflow: hidden;
    position: relative;
    width: 100%;
  }
  canvas { display: block; }
  .trike-hud {
    display: grid;
    gap: 8px;
    left: 10px;
    pointer-events: none;
    position: absolute;
    right: 10px;
    top: 10px;
    z-index: 1;
  }
  .hud-card {
    background: color-mix(in srgb, var(--panel) 84%, transparent);
    border: 1px solid var(--panel-border);
    border-radius: 8px;
    box-shadow: 0 12px 34px rgba(0, 0, 0, 0.24);
    padding: 10px;
  }
  .hud-kicker {
    color: var(--accent);
    font-size: 0.62rem;
    font-weight: 800;
    letter-spacing: 0.08em;
    margin: 0 0 3px;
    text-transform: uppercase;
  }
  h2, p { margin: 0; }
  h2 { font-size: 1rem; line-height: 1.1; }
  .hud-subtitle {
    color: var(--muted);
    font-size: 0.72rem;
    margin-top: 3px;
  }
  .hud-grid {
    display: grid;
    font-family: "Cascadia Mono", "SFMono-Regular", Consolas, monospace;
    font-size: 0.72rem;
    gap: 5px 10px;
    grid-template-columns: 1fr auto;
    margin-top: 10px;
  }
  .hud-grid span { color: var(--muted); }
  .hud-grid strong { color: var(--fg); font-weight: 800; text-align: right; }
</style>
