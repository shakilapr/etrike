import type { StreamMessage } from "./ws-types";
import type { Bus } from "./can-decoder";

export { type StreamMessage };

export interface StreamHandle {
  setFilter: (ids: string[]) => void;
  close: () => void;
}

const INITIAL_DELAY_MS = 500;
const MAX_DELAY_MS = 10_000;
const JITTER_MS = 1_000;

export function connectStream(
  onMessage: (message: StreamMessage) => void,
  onState: (connected: boolean) => void
): StreamHandle {
  let socket: WebSocket | null = null;
  let closed = false;
  let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  let attempt = 0;
  let pendingFilter: { buses: Bus[]; ids: string[]; keys: string[] } | null = null;

  const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
  const explicitBase = import.meta.env.VITE_WS_URL as string | undefined;
  const url = explicitBase ?? `${protocol}//${window.location.host}/ws`;

  function connect(): void {
    if (closed) return;

    socket = new WebSocket(url);
    const activeSocket = socket;

    activeSocket.addEventListener("open", () => {
      attempt = 0;

      // Re-apply pending filter BEFORE notifying connected state,
      // so no unfiltered frames arrive before the server processes the filter.
      if (pendingFilter && activeSocket.readyState === WebSocket.OPEN) {
        activeSocket.send(JSON.stringify({ type: "filter", ...pendingFilter }));
      }
      onState(true);
    });

    activeSocket.addEventListener("close", () => {
      onState(false);
      scheduleReconnect();
    });

    activeSocket.addEventListener("error", () => {
      onState(false);
      // close event will fire after error, triggering reconnect
    });

    activeSocket.addEventListener("message", (event) => {
      try {
        onMessage(JSON.parse(event.data) as StreamMessage);
      } catch {
        onMessage({ type: "status", payload: { warning: "invalid stream message" } });
      }
    });
  }

  function scheduleReconnect(): void {
    if (closed) return;
    if (reconnectTimer) return; // already scheduled

    const delay = Math.min(INITIAL_DELAY_MS * Math.pow(2, attempt), MAX_DELAY_MS);
    const jitter = Math.random() * JITTER_MS;
    attempt += 1;

    reconnectTimer = setTimeout(() => {
      reconnectTimer = null;
      connect();
    }, delay + jitter);
  }

  connect();

  return {
    setFilter: (ids: string[]) => {
      pendingFilter = normalizeFilter(ids);
      if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify({ type: "filter", ...pendingFilter }));
      }
    },
    close: () => {
      closed = true;
      if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
      }
      if (socket) {
        socket.close();
        socket = null;
      }
    }
  };
}

export function normalizeFilter(keys: string[]): { buses: Bus[]; ids: string[]; keys: string[] } {
  const buses = new Set<Bus>();
  const ids = new Set<string>();
  const scopedKeys = new Set<string>();

  for (const key of keys) {
    const [maybeBus, maybeId] = key.split(":");
    if ((maybeBus === "high" || maybeBus === "low") && maybeId) {
      buses.add(maybeBus);
      scopedKeys.add(`${maybeBus}:${maybeId}`);
    } else if (key) {
      ids.add(key);
    }
  }

  return {
    buses: [...buses],
    ids: [...ids],
    keys: [...scopedKeys]
  };
}
