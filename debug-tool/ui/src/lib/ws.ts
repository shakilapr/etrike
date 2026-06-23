import type { BackendStatus } from "./api";
import type { CanFrame, CanStats } from "./can-decoder";

export type StreamMessage =
  | { type: "can_frame"; payload: CanFrame }
  | { type: "stats"; payload: CanStats }
  | { type: "cmd_ack"; payload: Record<string, unknown> }
  | { type: "status"; payload: Partial<BackendStatus> & Record<string, unknown> };

export interface StreamHandle {
  setFilter: (ids: string[]) => void;
  close: () => void;
}

export function connectStream(onMessage: (message: StreamMessage) => void, onState: (connected: boolean) => void): StreamHandle {
  const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
  const explicitBase = import.meta.env.VITE_WS_URL as string | undefined;
  const url = explicitBase ?? `${protocol}//${window.location.host}/ws`;
  const socket = new WebSocket(url);

  socket.addEventListener("open", () => onState(true));
  socket.addEventListener("close", () => onState(false));
  socket.addEventListener("error", () => onState(false));
  socket.addEventListener("message", (event) => {
    try {
      onMessage(JSON.parse(event.data) as StreamMessage);
    } catch {
      onMessage({ type: "status", payload: { warning: "invalid stream message" } });
    }
  });

  return {
    setFilter: (ids: string[]) => {
      if (socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify({ type: "filter", ids }));
      }
    },
    close: () => socket.close()
  };
}
