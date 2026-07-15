export interface RobotState {
  x: number;
  y: number;
  theta: number;
  v: number;
  alpha: number;
}

export const globalRobotState: RobotState = {
  x: 0,
  y: 0,
  theta: -Math.PI / 2,
  v: 0,
  alpha: 0
};

export const robotParams = {
  L: 120,
  W: 80,
  MAX_ALPHA: Math.PI / 3,
  MAX_V: 300,
  ACCEL: 200,
  STEER_RATE: 2.0,
  FRICTION: 0.98
};

let lastTime = performance.now();
let targetV = 0;
let targetAlpha = 0;

export function setRobotTargets(speedMmps: number, steerDeg: number) {
  targetV = speedMmps;
  targetAlpha = (steerDeg * Math.PI) / 180;
}

function simulationLoop(timestamp: number) {
  const dt = Math.min((timestamp - lastTime) / 1000, 0.05);
  lastTime = timestamp;

  const p = robotParams;
  const current = globalRobotState;
  
  current.v = targetV;
  current.alpha = targetAlpha;

  const thetaDot = (current.v / p.L) * Math.tan(current.alpha);
  current.theta += thetaDot * dt;
  current.theta = ((current.theta + Math.PI) % (Math.PI * 2) + Math.PI * 2) % (Math.PI * 2) - Math.PI;

  current.x += current.v * Math.cos(current.theta) * dt;
  current.y += current.v * Math.sin(current.theta) * dt;

  requestAnimationFrame(simulationLoop);
}
requestAnimationFrame(simulationLoop);

export function roundedRectPath(ctx: CanvasRenderingContext2D, x: number, y: number, width: number, height: number, radius: number) {
  const r = Math.min(radius, width / 2, height / 2);
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.arcTo(x + width, y, x + width, y + height, r);
  ctx.arcTo(x + width, y + height, x, y + height, r);
  ctx.arcTo(x, y + height, x, y, r);
  ctx.arcTo(x, y, x + width, y, r);
  ctx.closePath();
}

export function drawWorldGrid(
  ctx: CanvasRenderingContext2D, 
  width: number, 
  height: number, 
  centerX: number, 
  centerY: number, 
  egoScreenY: number, 
  gridSize: number, 
  compact = false
) {
  const originX = width / 2 - centerX;
  const originY = egoScreenY - centerY;
  const offsetX = ((originX % gridSize) + gridSize) % gridSize;
  const offsetY = ((originY % gridSize) + gridSize) % gridSize;

  ctx.strokeStyle = compact ? '#e5eaf0' : '#e2e8f0';
  ctx.lineWidth = 1;
  ctx.beginPath();
  for (let x = offsetX - gridSize; x < width + gridSize; x += gridSize) {
    ctx.moveTo(x, 0); ctx.lineTo(x, height);
  }
  for (let y = offsetY - gridSize; y < height + gridSize; y += gridSize) {
    ctx.moveTo(0, y); ctx.lineTo(width, y);
  }
  ctx.stroke();

  if (!compact) {
    ctx.lineWidth = 1.5;
    ctx.strokeStyle = 'rgba(27,31,35,.18)';
    ctx.beginPath();
    ctx.moveTo(0, originY); ctx.lineTo(width, originY);
    ctx.moveTo(originX, 0); ctx.lineTo(originX, height);
    ctx.stroke();

    ctx.fillStyle = 'rgba(27,31,35,.48)';
    ctx.font = '11px ui-monospace, SFMono-Regular, Menlo, Consolas, monospace';
    if (originX > -120 && originX < width + 20 && originY > -20 && originY < height + 20) {
      ctx.fillText('Global origin (0,0)', originX + 6, originY - 7);
    }
  }
}

export function drawVehicle(
  ctx: CanvasRenderingContext2D, 
  robotState: RobotState,
  anchorX: number, 
  anchorY: number, 
  scale: number, 
  options: { compact?: boolean } = {}
) {
  const p = robotParams;
  const L = p.L * scale;
  const W = p.W * scale;
  const compact = !!options.compact;

  ctx.save();
  ctx.translate(anchorX, anchorY);

  if (!compact) {
    ctx.strokeStyle = 'rgba(27,31,35,.25)';
    ctx.setLineDash([5, 5]);
    ctx.beginPath();
    ctx.moveTo(0, 0); ctx.lineTo(150 * scale, 0);
    ctx.stroke();
  }

  ctx.rotate(robotState.theta);

  if (Math.abs(robotState.alpha) > 0.01) {
    const R = L / Math.tan(robotState.alpha);
    ctx.strokeStyle = compact ? 'rgba(126,68,187,.34)' : 'rgba(168,85,247,.42)';
    ctx.setLineDash(compact ? [5, 4] : [8, 4]);
    ctx.beginPath();
    ctx.moveTo(0, 0); ctx.lineTo(0, R);
    ctx.moveTo(L, 0); ctx.lineTo(0, R);
    ctx.stroke();
    if (!compact && Math.abs(R) < 1200 * scale) {
      ctx.fillStyle = '#7e22ce';
      ctx.beginPath();
      ctx.arc(0, R, 4, 0, Math.PI * 2);
      ctx.fill();
      ctx.font = `${Math.max(10, 12 * scale)}px ui-monospace, monospace`;
      ctx.fillText('C (ICR)', 10, R - 7);
    }
    ctx.setLineDash([]);
  }

  const chassisWidth = L + 40 * scale;
  const chassisHeight = W + 40 * scale;
  roundedRectPath(ctx, -30 * scale, -chassisHeight / 2, chassisWidth, chassisHeight, 14 * scale);
  ctx.fillStyle = 'rgba(255,255,255,.92)';
  ctx.fill();
  ctx.strokeStyle = '#334155';
  ctx.lineWidth = compact ? 1.4 : 2;
  ctx.stroke();

  ctx.strokeStyle = '#94a3b8';
  ctx.setLineDash([4, 4]);
  ctx.beginPath();
  ctx.moveTo(-40 * scale, 0); ctx.lineTo(L + 60 * scale, 0);
  ctx.stroke();
  ctx.setLineDash([]);

  ctx.beginPath();
  ctx.moveTo(0, -W / 2); ctx.lineTo(0, W / 2);
  ctx.stroke();

  const wheelLength = 34 * scale;
  const wheelThickness = 14 * scale;
  ctx.fillStyle = '#34d399';
  ctx.strokeStyle = '#1f6f52';
  ctx.fillRect(-wheelLength / 2, -W / 2 - wheelThickness / 2, wheelLength, wheelThickness);
  ctx.strokeRect(-wheelLength / 2, -W / 2 - wheelThickness / 2, wheelLength, wheelThickness);
  ctx.fillRect(-wheelLength / 2, W / 2 - wheelThickness / 2, wheelLength, wheelThickness);
  ctx.strokeRect(-wheelLength / 2, W / 2 - wheelThickness / 2, wheelLength, wheelThickness);

  ctx.save();
  ctx.translate(L, 0);
  if (!compact) {
    ctx.strokeStyle = 'rgba(27,31,35,.25)';
    ctx.setLineDash([5, 5]);
    ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(80 * scale, 0); ctx.stroke();
    ctx.setLineDash([]);
  }
  ctx.rotate(robotState.alpha);
  ctx.fillStyle = '#ef4444';
  ctx.strokeStyle = '#9f252c';
  ctx.fillRect(-wheelLength / 2, -wheelThickness / 2, wheelLength, wheelThickness);
  ctx.strokeRect(-wheelLength / 2, -wheelThickness / 2, wheelLength, wheelThickness);

  if (Math.abs(robotState.v) > 1) {
    const velocityLength = (robotState.v / p.MAX_V) * 64 * scale;
    ctx.strokeStyle = '#a855f7';
    ctx.lineWidth = compact ? 1.5 : 2;
    ctx.beginPath();
    ctx.moveTo(0, 0); ctx.lineTo(velocityLength, 0);
    const direction = velocityLength >= 0 ? 1 : -1;
    ctx.lineTo(velocityLength - 7 * scale * direction, -4 * scale);
    ctx.moveTo(velocityLength, 0);
    ctx.lineTo(velocityLength - 7 * scale * direction, 4 * scale);
    ctx.stroke();
  }
  ctx.restore();

  if (!compact) {
    ctx.save();
    ctx.rotate(-robotState.theta);
    const drawTheta = robotState.theta;
    ctx.beginPath();
    ctx.strokeStyle = '#2563eb';
    ctx.lineWidth = 2;
    ctx.arc(0, 0, 60 * scale, Math.min(0, drawTheta), Math.max(0, drawTheta));
    ctx.stroke();
    ctx.fillStyle = '#2563eb';
    ctx.font = `${Math.max(14, 16 * scale)}px serif`;
    ctx.fillText('θ', 70 * scale * Math.cos(drawTheta / 2), 70 * scale * Math.sin(drawTheta / 2) + 5);
    ctx.restore();

    ctx.save();
    ctx.translate(L, 0);
    if (Math.abs(robotState.alpha) > 0.02) {
      ctx.beginPath();
      ctx.strokeStyle = '#dc2626';
      ctx.lineWidth = 2;
      ctx.arc(0, 0, 50 * scale, Math.min(0, robotState.alpha), Math.max(0, robotState.alpha));
      ctx.stroke();
      ctx.fillStyle = '#dc2626';
      ctx.font = `${Math.max(14, 16 * scale)}px serif`;
      ctx.fillText('α', 60 * scale * Math.cos(robotState.alpha / 2), 60 * scale * Math.sin(robotState.alpha / 2) + 5);
    }
    ctx.restore();

    ctx.fillStyle = '#1b1f23';
    ctx.font = `${Math.max(11, 13 * scale)}px ui-sans-serif, sans-serif`;
    ctx.fillText('L', L / 2 - 4, -10 * scale);
  }

  ctx.restore();
}
