import { useState, useEffect, useRef } from 'react';
import type { RobotState } from '../utils/robotDrawing';
import { robotParams } from '../utils/robotDrawing';

export function useKinematicRobot(speedMmps: number, steerDeg: number): RobotState {
  const [state, setState] = useState<RobotState>({
    x: 0,
    y: 0,
    theta: -Math.PI / 2,
    v: 0,
    alpha: 0
  });

  const stateRef = useRef(state);
  const lastTimeRef = useRef<number>(performance.now());
  const reqRef = useRef<number>(0);

  useEffect(() => {
    const loop = (timestamp: number) => {
      const dt = Math.min((timestamp - lastTimeRef.current) / 1000, 0.05);
      lastTimeRef.current = timestamp;

      const p = robotParams;
      const current = stateRef.current;
      
      // Target values from telemetry
      const targetV = speedMmps; // mm/s could map directly to px/s for preview
      const targetAlpha = (steerDeg * Math.PI) / 180;

      // Smoothly interpolate towards target (or just snap to it since it's telemetry)
      const v = targetV;
      const alpha = targetAlpha;

      const thetaDot = (v / p.L) * Math.tan(alpha);
      const theta = ((current.theta + thetaDot * dt + Math.PI) % (Math.PI * 2) + Math.PI * 2) % (Math.PI * 2) - Math.PI;

      const x = current.x + v * Math.cos(theta) * dt;
      const y = current.y + v * Math.sin(theta) * dt;

      const newState = { x, y, theta, v, alpha };
      stateRef.current = newState;
      setState(newState);

      reqRef.current = requestAnimationFrame(loop);
    };

    reqRef.current = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(reqRef.current);
  }, [speedMmps, steerDeg]);

  return state;
}
