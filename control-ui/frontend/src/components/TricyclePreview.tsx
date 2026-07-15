import React, { useRef, useEffect } from 'react';
import { useCanStore } from '../store/useCanStore';
import { globalRobotState, drawWorldGrid, drawVehicle, setRobotTargets } from '../utils/robotDrawing';

interface TricyclePreviewProps {
  keyboardEnabled: boolean;
  pressedKeys: { forward: boolean; backward: boolean; left: boolean; right: boolean };
  onToggleKeyboard: () => void;
}

export function TricyclePreview({ keyboardEnabled, pressedKeys, onToggleKeyboard }: TricyclePreviewProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const channels = useCanStore(s => s.channels);

  // Read from actual decoded telemetry frames
  const sesFrame = channels['0']?.['0x201'] || channels['1']?.['0x201'];
  const mtrFrame = channels['0']?.['0x206'] || channels['1']?.['0x206'];

  const steerDeg = (sesFrame?.signals?.['angle_deg'] as number) ?? 0;
  const speedMmps = (mtrFrame?.signals?.['actual_speed_mmps'] as number) ?? 0;
  const gearState = (mtrFrame?.signals?.['gear_state'] as number) ?? 0;

  const gearNames = ['N', 'D', 'S', 'R'];

  useEffect(() => {
    setRobotTargets(speedMmps, steerDeg);
  }, [speedMmps, steerDeg]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    let animFrame: number;

    function draw() {
      if (!ctx || !canvas) return;

      const W = canvas.width;
      const H = canvas.height;

      // Background
      ctx.clearRect(0, 0, W, H);
      ctx.fillStyle = '#f8fafc';
      ctx.fillRect(0, 0, W, H);

      const egoScreenY = H * 0.72;
      const scale = Math.max(0.72, Math.min(1.0, W / 900, H / 480));
      
      if (Math.random() < 0.05) {
        console.log(`Main Canvas - W: ${W}, H: ${H}, egoY: ${egoScreenY}, scale: ${scale}, x: ${globalRobotState.x}, y: ${globalRobotState.y}`);
      }

      drawWorldGrid(ctx, W, H, globalRobotState.x, globalRobotState.y, egoScreenY, 50, false);
      drawVehicle(ctx, globalRobotState, W / 2, egoScreenY, scale, { compact: false });

      // Update HUD directly
      const elV = document.getElementById('tel-v');
      const elTheta = document.getElementById('tel-theta');
      const elAlpha = document.getElementById('tel-alpha');
      const elRadius = document.getElementById('tel-radius');
      const elPosition = document.getElementById('tel-position');

      if (elV) elV.textContent = `${globalRobotState.v.toFixed(1)} px/s`;
      if (elAlpha) elAlpha.textContent = `${(globalRobotState.alpha * 180 / Math.PI).toFixed(1)}°`;
      if (elTheta) {
        let heading = globalRobotState.theta * 180 / Math.PI;
        if (heading > 180) heading -= 360;
        if (heading < -180) heading += 360;
        elTheta.textContent = `${heading.toFixed(1)}°`;
      }
      if (elPosition) elPosition.textContent = `${globalRobotState.x.toFixed(1)}, ${globalRobotState.y.toFixed(1)}`;
      if (elRadius) {
        elRadius.textContent = Math.abs(globalRobotState.alpha) < 0.01
          ? 'Straight (∞)'
          : `${Math.abs(120 / Math.tan(globalRobotState.alpha)).toFixed(1)} px`;
      }

      animFrame = requestAnimationFrame(draw);
    }

    animFrame = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(animFrame);
  }, []);

  // Handle canvas resize
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ro = new ResizeObserver(() => {
      canvas.width = canvas.offsetWidth;
      canvas.height = canvas.offsetHeight;
    });
    ro.observe(canvas);
    canvas.width = canvas.offsetWidth;
    canvas.height = canvas.offsetHeight;
    return () => ro.disconnect();
  }, []);

  return (
    <div className="robot-workspace" id="robot-preview">
      <div className="robot-workspace-header">
        <div className="robot-workspace-title">
          <h2>Tricycle Kinematic Preview</h2>
          <p>Ego-centric view — driven by decoded CAN telemetry (SES_STATUS 0x201 + MTR_MOTOR_FBK 0x206)</p>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
          {keyboardEnabled && (
            <span style={{ display: 'flex', alignItems: 'center', gap: 6, color: 'var(--success)', fontSize: 11.5, fontWeight: 600 }}>
              <span className="status-dot live" />
              WASD Active
            </span>
          )}
          <button
            className={`send-btn ${keyboardEnabled ? 'secondary' : ''}`}
            id="kb-toggle"
            style={{ height: 32, fontSize: 12 }}
            onClick={onToggleKeyboard}
          >
            {keyboardEnabled ? '⌨ Disable KB' : '⌨ Enable KB'}
          </button>
        </div>
      </div>

      <div className="robot-preview-stage" tabIndex={0} id="preview-stage" aria-label="Tricycle preview">
        <canvas ref={canvasRef} style={{ display: 'block', width: '100%', height: '100%' }} />

        <div className="robot-hud" aria-hidden="true">
          <section className="robot-hud-card" id="telemetry-hud">
            <h3>Live Telemetry</h3>
            <dl className="robot-telemetry-grid telemetry-grid" style={{ display: 'grid', gridTemplateColumns: '1fr auto', gap: '7px 18px', padding: '11px 12px 12px', font: '11px/1.25 ui-monospace, SFMono-Regular, Menlo, Consolas, monospace', fontVariantNumeric: 'tabular-nums' }}>
              <dt style={{ color: 'var(--text-tertiary)', fontWeight: 650 }}>Velocity (v)</dt>
              <dd id="tel-v" style={{ margin: 0, textAlign: 'right' }}>0.0 px/s</dd>
              <dt style={{ color: 'var(--text-tertiary)', fontWeight: 650 }}>Heading (θ)</dt>
              <dd id="tel-theta" style={{ margin: 0, textAlign: 'right' }}>-90.0°</dd>
              <dt style={{ color: 'var(--text-tertiary)', fontWeight: 650 }}>Steering (α)</dt>
              <dd id="tel-alpha" style={{ margin: 0, textAlign: 'right' }}>0.0°</dd>
              <dt style={{ color: 'var(--text-tertiary)', fontWeight: 650 }}>Turn radius (ρ)</dt>
              <dd id="tel-radius" style={{ margin: 0, textAlign: 'right' }}>Straight (∞)</dd>
              <dt style={{ color: 'var(--text-tertiary)', fontWeight: 650 }}>Position</dt>
              <dd id="tel-position" style={{ margin: 0, textAlign: 'right' }}>0.0, 0.0</dd>
              <dt style={{ color: 'var(--text-tertiary)', fontWeight: 650, marginTop: 4 }}>Gear State</dt>
              <dd id="tel-gear" style={{ margin: 0, textAlign: 'right', marginTop: 4 }}>{gearNames[gearState] ?? '–'}</dd>
            </dl>
          </section>

          <section className="robot-hud-card controls-help" id="controls-hud">
            <h3>Keyboard</h3>
            <ul>
              <li><span className="kbd">W</span><span className="kbd">S</span> Speed</li>
              <li><span className="kbd">A</span><span className="kbd">D</span> Steer</li>
              <li><span className="kbd">Space</span> Stop</li>
            </ul>
          </section>
        </div>
      </div>

      <div className="robot-preview-footer">
        <span>Source: SES_STATUS 0x201 · MTR_MOTOR_FBK 0x206</span>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          {pressedKeys.forward && <span className="kbd">W↑</span>}
          {pressedKeys.backward && <span className="kbd">S↓</span>}
          {pressedKeys.left && <span className="kbd">A←</span>}
          {pressedKeys.right && <span className="kbd">D→</span>}
        </div>
      </div>
    </div>
  );
}
