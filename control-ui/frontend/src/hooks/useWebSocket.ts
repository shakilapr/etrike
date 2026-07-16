import { useEffect, useRef, useState } from 'react';
import { useCanStore } from '../store/useCanStore';

export function useWebSocket(url: string) {
  const [status, setStatus] = useState<'connecting' | 'connected' | 'disconnected'>('disconnected');
  const ws = useRef<WebSocket | null>(null);
  const reconnectTimeout = useRef<NodeJS.Timeout | null>(null);
  const updateBatch = useCanStore(state => state.updateBatch);
  const ageFrames = useCanStore(state => state.ageFrames);
  
  useEffect(() => {
    let isMounted = true;
    let reconnectDelay = 1000;
    const maxDelay = 10000;

    const connect = () => {
      setStatus('connecting');
      ws.current = new WebSocket(url);

      ws.current.onopen = () => {
        if (!isMounted) return;
        setStatus('connected');
        reconnectDelay = 1000; // reset backoff
      };

      ws.current.onmessage = (event) => {
        if (!isMounted) return;
        try {
          const batch = JSON.parse(event.data);
          updateBatch(batch);
        } catch (e) {
          console.error("Failed to parse websocket message", e);
        }
      };

      ws.current.onclose = () => {
        if (!isMounted) return;
        setStatus('disconnected');
        // Exponential backoff reconnect
        reconnectTimeout.current = setTimeout(() => {
          reconnectDelay = Math.min(reconnectDelay * 1.5, maxDelay);
          connect();
        }, reconnectDelay);
      };
      
      ws.current.onerror = (error) => {
        console.error("WebSocket error:", error);
        ws.current?.close(); // Force trigger onclose
      };
    };

    connect();
    
    // Set up a 10Hz aging loop for UI animations
    const agingInterval = setInterval(() => {
      ageFrames();
    }, 100);

    return () => {
      isMounted = false;
      clearInterval(agingInterval);
      if (reconnectTimeout.current) clearTimeout(reconnectTimeout.current);
      if (ws.current) {
        ws.current.onclose = null; // Prevent reconnect on intentional unmount
        ws.current.close();
      }
    };
  }, [url, updateBatch, ageFrames]);

  return status;
}
