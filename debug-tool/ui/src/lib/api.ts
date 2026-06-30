import type { Bus, CanFrame, CanMessageDef, CanStats, InjectionTemplate } from "./can-decoder";

const API_BASE = import.meta.env.VITE_API_BASE ?? "";
const FETCH_TIMEOUT_MS = 10_000;

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), FETCH_TIMEOUT_MS);
  const hasBody = init?.body != null;
  try {
    const response = await fetch(`${API_BASE}${path}`, {
      headers: hasBody
        ? { "content-type": "application/json", ...(init?.headers ?? {}) }
        : { ...(init?.headers ?? {}) },
      signal: controller.signal,
      ...init
    });

    if (!response.ok) {
      const text = await response.text();
      let message = text || response.statusText;
      try {
        const payload = JSON.parse(text) as { error?: unknown };
        if (typeof payload.error === "string") message = payload.error;
        else if (payload.error) message = JSON.stringify(payload.error);
      } catch {
        // Keep the raw response text for non-JSON errors.
      }
      throw new Error(message);
    }

    return (await response.json()) as T;
  } finally {
    clearTimeout(timer);
  }
}

export interface BackendStatus {
  backend_online: boolean;
  started_at: number;
  uptime_s: number;
  adapter_connected: boolean;
  esp32_connected: boolean;
  last_status_at: number | null;
  bridge?: {
    transport: "serial" | "canalystii" | "mqtt" | "disabled";
    adapter: string;
    connected: boolean;
    link_open: boolean;
    path: string | null;
    baud_rate: number | null;
    bitrate: number | null;
    last_status_at: number | null;
    last_error: string | null;
  };
  serial: {
    port_open: boolean;
    path: string | null;
    baud_rate: number;
    last_error: string | null;
  };
  bus_detection?: {
    detected: boolean;
    bus: string;
    confidence: "none" | "low" | "high";
    highHits: number;
    lowHits: number;
  };
  bus_stats: CanStats["buses"];
  websocket_clients: number;
  storage: {
    frames: number;
    injected: number;
    recordings: number;
  };
}

export interface CommandResponse {
  cmd: string;
  bus: Bus;
  id: string;
  status: string;
}

export function getStatus(): Promise<BackendStatus> {
  return request<BackendStatus>("/api/status");
}

export async function getCanIds(): Promise<CanMessageDef[]> {
  const payload = await request<{ ids: CanMessageDef[] }>("/api/can/ids");
  return payload.ids;
}

export async function getFrames(limit = 300, bus?: Bus): Promise<CanFrame[]> {
  const query = new URLSearchParams({ limit: String(limit) });
  if (bus) query.set("bus", bus);
  const payload = await request<{ frames: CanFrame[] }>(`/api/can/frames?${query.toString()}`);
  return payload.frames.reverse();
}

export async function getStats(): Promise<CanStats> {
  const payload = await request<{ stats: CanStats }>("/api/can/stats");
  return payload.stats;
}

export async function getTemplates(): Promise<InjectionTemplate[]> {
  const payload = await request<{ templates: InjectionTemplate[] }>("/api/templates");
  return payload.templates;
}

export function sendFrame(payload: {
  bus: Bus;
  id: string;
  dlc: number;
  data: number[];
  confirm_estop?: boolean;
}): Promise<CommandResponse> {
  return request<CommandResponse>("/api/cmd/send", {
    method: "POST",
    body: JSON.stringify(payload)
  });
}

export function startPeriodic(payload: {
  bus: Bus;
  id: string;
  dlc: number;
  data: number[];
  interval_ms: number;
  count?: number;
  confirm_estop?: boolean;
}): Promise<CommandResponse> {
  return request<CommandResponse>("/api/cmd/periodic", {
    method: "POST",
    body: JSON.stringify({ action: "start", ...payload })
  });
}

export function stopPeriodic(bus: Bus, id: string): Promise<CommandResponse> {
  return request<CommandResponse>("/api/cmd/periodic", {
    method: "POST",
    body: JSON.stringify({ action: "stop", bus, id })
  });
}

export function clearFrames(): Promise<{ ok: boolean }> {
  return request("/api/can/frames", { method: "DELETE" });
}

export function restartBridge(): Promise<{ ok: boolean }> {
  return request("/api/system/restart", { method: "POST" });
}

export function stopBridge(): Promise<{ ok: boolean }> {
  return request("/api/system/stop", { method: "POST" });
}

export function getPipelineChains(): Promise<{ chains: unknown[] }> {
  return request("/api/can/pipeline");
}
