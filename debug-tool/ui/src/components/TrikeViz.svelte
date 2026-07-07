<script lang="ts">
  import { onMount, onDestroy } from "svelte";
  import { latestById } from "../stores/can";

  export let visible = false;

  let x = 0, y = 0, theta = 0, v = 0, alpha = 0;
  const L = 120, MAX_A = Math.PI / 3, FRICTION = 0.97;
  let canvas: HTMLCanvasElement;
  let ctx: CanvasRenderingContext2D;
  let animId = 0;
  let lastTime = performance.now();
  let w = 320, h = 400;
  let speedKmh = 0, headingDeg = 0, steerDeg = 0, turnRadius = "∞";
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

    unsub = latestById.subscribe(($latest) => {
      const f120 = $latest["low:0x120"]?.decoded;
      const f206 = $latest["low:0x206"]?.decoded ?? $latest["high:0x206"]?.decoded;
      const gear = (f206 as any)?.gear_state ?? 1;
      let raw = 0;
      if (f120 && typeof (f120 as any).speed_mmps === "number") raw = (f120 as any).speed_mmps;
      else if (f206 && typeof (f206 as any).actual_speed_mmps === "number") raw = (f206 as any).actual_speed_mmps;
      if (gear === 3) raw = -Math.abs(raw);
      if (gear === 0) raw *= 0.3;
      const ses = $latest["low:0x201"]?.decoded;
      const diag = $latest["high:0x310"]?.decoded;
      if (ses && typeof (ses as any).str_angle === "number") alpha = ((ses as any).str_angle * 0.1) * Math.PI / 180;
      else if (diag && typeof (diag as any).SteerDiag_Angle0_1deg === "number") alpha = (diag as any).SteerDiag_Angle0_1deg * Math.PI / 180;
      alpha = Math.max(-MAX_A, Math.min(MAX_A, alpha));
      v = raw * 0.25;
      speedKmh = raw * 3.6 / 1000;
      headingDeg = (theta * 180 / Math.PI) % 360;
      steerDeg = alpha * 180 / Math.PI;
      turnRadius = Math.abs(alpha) < 0.005 ? "∞" : (L / Math.tan(alpha)).toFixed(0) + " px";
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
    x += v * Math.cos(theta) * dt;
    y += v * Math.sin(theta) * dt;
    if (Math.abs(v) < 0.5) v *= FRICTION;
    else if (Math.abs(v) < 2 && Math.abs(alpha) < 0.01) alpha *= 0.95;
  }

  function draw() {
    const cx = w / 2, cy = h / 2;
    ctx.clearRect(0, 0, w, h);
    const gs = 40;
    const ox = ((-x % gs) + gs) % gs, oy = ((-y % gs) + gs) % gs;
    ctx.strokeStyle = "#334155"; ctx.lineWidth = 0.5;
    ctx.beginPath();
    for (let gx = ox; gx < w; gx += gs) { ctx.moveTo(gx, 0); ctx.lineTo(gx, h); }
    for (let gy = oy; gy < h; gy += gs) { ctx.moveTo(0, gy); ctx.lineTo(w, gy); }
    ctx.stroke();
    ctx.save(); ctx.translate(cx, cy);
    ctx.rotate(theta);
    const cW = L + 40, cH = 70;
    ctx.fillStyle = "rgba(30,41,59,0.9)"; ctx.strokeStyle = "#64748b"; ctx.lineWidth = 1.5;
    ctx.beginPath(); ctx.roundRect(-30, -cH / 2, cW, cH, 10); ctx.fill(); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(0, -35); ctx.lineTo(0, 35); ctx.stroke();
    ctx.fillStyle = "#22c55e"; ctx.fillRect(-16, -42, 32, 12); ctx.fillRect(-16, 30, 32, 12);
    ctx.save(); ctx.translate(L, 0); ctx.rotate(alpha);
    ctx.fillStyle = "#ef4444"; ctx.fillRect(-16, -6, 32, 12);
    if (Math.abs(v) > 2) {
      const vl = Math.min(Math.abs(v) * 0.3, 80);
      ctx.strokeStyle = "#8b5cf6"; ctx.lineWidth = 2;
      ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(vl, 0); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(vl, 0); ctx.lineTo(vl - 6, -4); ctx.moveTo(vl, 0); ctx.lineTo(vl - 6, 4); ctx.stroke();
    }
    ctx.restore();
    if (Math.abs(alpha) > 0.01) {
      const R = L / Math.tan(alpha);
      ctx.strokeStyle = "rgba(168,85,247,0.4)"; ctx.setLineDash([6, 4]);
      ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(0, R); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(L, 0); ctx.lineTo(0, R); ctx.stroke(); ctx.setLineDash([]);
      ctx.fillStyle = "#a855f7"; ctx.beginPath(); ctx.arc(0, R, 3, 0, Math.PI * 2); ctx.fill();
    }
    ctx.rotate(-theta);
    if (Math.abs(theta) > 0.01) { ctx.strokeStyle = "#2563eb"; ctx.lineWidth = 1.5; ctx.beginPath(); ctx.arc(0, 0, 55, Math.min(0, theta), Math.max(0, theta)); ctx.stroke(); }
    ctx.restore();
    ctx.fillStyle = "rgba(0,0,0,0.65)"; ctx.fillRect(6, 6, 150, 72);
    ctx.fillStyle = "#f8fafc"; ctx.font = "11px monospace";
    ctx.fillText(`Speed   ${speedKmh.toFixed(1)} km/h`, 14, 24);
    ctx.fillText(`Heading ${headingDeg.toFixed(1)}°`, 14, 40);
    ctx.fillText(`Steer   ${steerDeg.toFixed(1)}°`, 14, 56);
    ctx.fillText(`Radius  ${turnRadius}`, 14, 72);
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
  <canvas bind:this={canvas}></canvas>
</div>

<style>
  .trike-viz { width: 100%; height: 100%; min-height: 300px; background: #0f172a; border-radius: 6px; overflow: hidden; }
  canvas { display: block; }
</style>
