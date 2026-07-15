import { useEffect, useRef, useState, useCallback } from 'react';
import { sendHostDriveCmd } from '../api/inject';

interface TeleoperationState {
  forward: boolean;
  backward: boolean;
  left: boolean;
  right: boolean;
}

export function useTeleoperation(enabled: boolean) {
  const [keys, setKeys] = useState<TeleoperationState>({
    forward: false,
    backward: false,
    left: false,
    right: false
  });
  
  const keysRef = useRef(keys);
  keysRef.current = keys;

  const intervalRef = useRef<NodeJS.Timeout | null>(null);

  const sendZeroCommand = useCallback(() => {
    sendHostDriveCmd(0, 0, 1); // 0 speed, 0 steering, 1 (Drive)
  }, []);

  useEffect(() => {
    if (!enabled) {
      sendZeroCommand();
      return;
    }

    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.repeat) return; // Ignore auto-repeat
      
      const key = e.key.toLowerCase();
      setKeys(prev => {
        const next = { ...prev };
        if (key === 'w' || key === 'arrowup') next.forward = true;
        if (key === 's' || key === 'arrowdown') next.backward = true;
        if (key === 'a' || key === 'arrowleft') next.left = true;
        if (key === 'd' || key === 'arrowright') next.right = true;
        return next;
      });
    };

    const handleKeyUp = (e: KeyboardEvent) => {
      const key = e.key.toLowerCase();
      setKeys(prev => {
        const next = { ...prev };
        if (key === 'w' || key === 'arrowup') next.forward = false;
        if (key === 's' || key === 'arrowdown') next.backward = false;
        if (key === 'a' || key === 'arrowleft') next.left = false;
        if (key === 'd' || key === 'arrowright') next.right = false;
        return next;
      });
    };

    const handleBlur = () => {
      // Safety interlock: if window loses focus, stop!
      setKeys({ forward: false, backward: false, left: false, right: false });
      sendZeroCommand();
    };

    window.addEventListener('keydown', handleKeyDown);
    window.addEventListener('keyup', handleKeyUp);
    window.addEventListener('blur', handleBlur);

    return () => {
      window.removeEventListener('keydown', handleKeyDown);
      window.removeEventListener('keyup', handleKeyUp);
      window.removeEventListener('blur', handleBlur);
      sendZeroCommand(); // Always stop on unmount
    };
  }, [enabled, sendZeroCommand]);

  useEffect(() => {
    if (!enabled) return;

    // Periodic TX loop (20Hz)
    intervalRef.current = setInterval(() => {
      const currentKeys = keysRef.current;
      const anyPressed = Object.values(currentKeys).some(Boolean);
      
      let speed = 0;
      let yaw = 0;
      
      if (currentKeys.forward) speed = 2000; // 2 m/s
      if (currentKeys.backward) speed = -1000; // -1 m/s
      
      if (currentKeys.left) yaw = 500; // 0.5 rad/s
      if (currentKeys.right) yaw = -500; // -0.5 rad/s
      
      // If nothing is pressed, we might still send a zero command once, 
      // but to save bandwidth we could skip it if we already sent zero.
      // For a real robot, continuous periodic heartbeat is required.
      sendHostDriveCmd(speed, yaw, 1);
      
    }, 50);

    return () => {
      if (intervalRef.current) clearInterval(intervalRef.current);
    };
  }, [enabled]);

  return keys;
}
