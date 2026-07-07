<script lang="ts">
  import { onMount, onDestroy } from "svelte";
  import { latestById } from "../stores/can";
  import type { CanFrame } from "../lib/can-decoder";

  // ── Physics state ──
  let x = 0, y = 0, theta = 0, v = 0, alpha = 0;
  const L = 120; // wheelbase (px)
  const MAX_ALPHA = Math.PI / 3; // 60°
  const FRICTION = 0.97;

  // ── Canvas ──
  let canvas: HTMLCanvasElement;
  let ctx: CanvasRenderingContext2D;
  let animId: number;
  let lastTime = performance.now();
  let w = 320, h = 400;

  // ── Display values ──
  let speedKmh = 0;
  let headingDeg = 0;
  let steerDeg = 0;
  let turnRadius = "∞";

  // ── Store subscription ──
  let unsub: () => void;

  onMount(() => {
    ctx = canvas.getContext("2d")!;
    resize();
    window.addEventListener("resize", resize);
    // Also observe parent for sidebar open/close transitions
    const observer = new ResizeObserver(() => resize());
    if (canvas.parentElement) observer.observe(canvas.parentElement);

    unsub = latestById.subscribe(($latest) => {
      // Extract speed: prefer 0x120 throttle, fall back to 0x206 motor feedback
      const throttle = $latest["low:0x120"]?.decoded;
      const motorFb = $latest["low:0x206"]?.decoded ?? $latest["high:0x206"]?.decoded;
      const gear = (motorFb as any)?.gear_state ?? 1;

      let rawSpeed = 0;
      if (throttle && typeof (throttle as any).speed_mmps === "number") {
        rawSpeed = (throttle as any).speed_mmps;
      } else if (motorFb && typeof (motorFb as any).actual_speed_mmps === "number") {
        rawSpeed = (motorFb as any).actual_speed_mmps;
      }
      // Reverse: flip sign
      if (gear === 3) rawSpeed = -Math.abs(rawSpeed);
      if (gear === 0) rawSpeed *= 0.3; // neutral drag

      // Extract steering: prefer 0x201 SES, fall back to 0x310 diag
      const ses = $latest["low:0x201"]?.decoded;
      const diag = $latest["high:0x310"]?.decoded;
      if (ses && typeof (ses as any).str_angle === "number") {
        alpha = ((ses as any).str_angle * 0.1) * Math.PI / 180; // 0.1° → rad
      } else if (diag && typeof (diag as any).SteerDiag_Angle0_1deg === "number") {
        alpha = (diag as any).SteerDiag_Angle0_1deg * Math.PI / 180;
      }

      // Scale speed for visualization (mm/s → px/s)
      v = rawSpeed * 0.25;
      // Clamp steering
      alpha = Math.max(-MAX_ALPHA, Math.min(MAX_ALPHA, alpha));

      // Update display values
      speedKmh = rawSpeed * 3.6 / 1000;
      headingDeg = (theta * 180 / Math.PI) % 360;
      steerDeg = alpha * 180 / Math.PI;
      if (Math.abs(alpha) < 0.005) turnRadius = "∞";
      else turnRadius = (L / Math.tan(alpha)).toFixed(0) + " px";
    });

    lastTime = performance.now();
    animId = requestAnimationFrame(loop);
  });

  let observer: ResizeObserver;

  onDestroy(() => {
    cancelAnimationFrame(animId);
    window.removeEventListener("resize", resize);
    observer?.disconnect();
    unsub?.();
  });

  function resize() {
    if (!canvas) return;
    const rect = canvas.parentElement?.getBoundingClientRect();
    w = rect?.width ?? 320;
    h = rect?.height ?? 400;
    canvas.width = w * devicePixelRatio;
    canvas.height = h * devicePixelRatio;
    canvas.style.width = w + "px";
    canvas.style.height = h + "px";
    ctx = canvas.getContext("2d")!;
    ctx.scale(devicePixelRatio, devicePixelRatio);
  }

  function update(dt: number) {
    if (dt > 0.1) return;
    const thetaDot = (v / L) * Math.tan(alpha);
    theta += thetaDot * dt;
    x += v * Math.cos(theta) * dt;
    y += v * Math.sin(theta) * dt;
    // Friction when no data
    if (Math.abs(v) < 0.5) v *= FRICTION;
    else if (Math.abs(v) < 2 && Math.abs(alpha) < 0.01) alpha *= 0.95;
  }

  function draw() {
    const cx = w / 2;
    const cy = h / 2;
    ctx.clearRect(0, 0, w, h);

    // Grid
    const gs = 40;
    const ox = (-x % gs + gs) % gs;
    const oy = (-y % gs + gs) % gs;
    ctx.strokeStyle = "#e2e8f0";
    ctx.lineWidth = 0.5;
    ctx.beginPath();
    for (let gx = ox; gx < w; gx += gs) { ctx.moveTo(gx, 0); ctx.lineTo(gx, h); }
    for (let gy = oy; gy < h; gy += gs) { ctx.moveTo(0, gy); ctx.lineTo(w, gy); }
    ctx.stroke();

    ctx.save();
    ctx.translate(cx, cy);

    // Vehicle
    ctx.rotate(theta);
    const cW = L + 40, cH = 70;
    ctx.fillStyle = "rgba(255,255,255,0.8)";
    ctx.strokeStyle = "#334155";
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.roundRect(-30, -cH / 2, cW, cH, 10);
    ctx.fill();
    ctx.stroke();

    // Rear axle
    ctx.beginPath(); ctx.moveTo(0, -35); ctx.lineTo(0, 35); ctx.stroke();
    // Rear wheels
    ctx.fillStyle = "#22c55e";
    ctx.fillRect(-16, -42, 32, 12);
    ctx.fillRect(-16, 30, 32, 12);

    // Front wheel (steered)
    ctx.save();
    ctx.translate(L, 0);
    ctx.rotate(alpha);
    ctx.fillStyle = "#ef4444";
    ctx.fillRect(-16, -6, 32, 12);
    // Velocity vector
    if (Math.abs(v) > 2) {
      const vl = Math.min(Math.abs(v) * 0.3, 80);
      ctx.strokeStyle = "#8b5cf6";
      ctx.lineWidth = 2;
      ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(vl, 0); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(vl, 0); ctx.lineTo(vl - 6, -4); ctx.moveTo(vl, 0); ctx.lineTo(vl - 6, 4); ctx.stroke();
    }
    ctx.restore();

    // ICR
    if (Math.abs(alpha) > 0.01) {
      const R = L / Math.tan(alpha);
      ctx.strokeStyle = "rgba(168,85,247,0.4)"; ctx.setLineDash([6, 4]);
      ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(0, R); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(L, 0); ctx.lineTo(0, R); ctx.stroke();
      ctx.setLineDash([]);
      ctx.fillStyle = "#a855f7"; ctx.beginPath(); ctx.arc(0, R, 3, 0, Math.PI * 2); ctx.fill();
    }

    // Theta arc
    ctx.rotate(-theta);
    if (Math.abs(theta) > 0.01) {
      ctx.strokeStyle = "#2563eb"; ctx.lineWidth = 1.5;
      ctx.beginPath(); ctx.arc(0, 0, 55, Math.min(0, theta), Math.max(0, theta)); ctx.stroke();
    }

    ctx.restore();
  }

  function drawHUD() {
    ctx.fillStyle = "rgba(0,0,0,0.65)";
    ctx.fillRect(6, 6, 150, 72);
    ctx.fillStyle = "#f8fafc";
    ctx.font = "11px monospace";
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
    drawHUD();
    animId = requestAnimationFrame(loop);
  }
</script>

<div class="trike-viz">
  <canvas bind:this={canvas}></canvas>
</div>

<style>
  .trike-viz { width: 100%; height: 100%; min-height: 300px; background: #0f172a; border-radius: 6px; overflow: hidden; }
  canvas { display: block; }
</style>
