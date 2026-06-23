import type { CanFrame, CanMessageDef, CanStats, InjectionTemplate } from "./can-decoder";

const API_BASE = import.meta.env.VITE_API_BASE ?? "";

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(`${API_BASE}${path}`, {
    headers: {
      "content-type": "application/json",
      ...(init?.headers ?? {})
    },
    ...init
  });

  if (!response.ok) {
    const text = await response.text();
    throw new Error(text || response.statusText);
  }

  return (await response.json()) as T;
}

export interface BackendStatus {
  backend_online: boolean;
  started_at: number;
  uptime_s: number;
  debug_esp32_online: boolean;
  debug_esp32_uptime_s: number | null;
  last_status_at: number | null;
  mqtt_connected: boolean;
  websocket_clients: number;
  storage: {
    frames: number;
    injected: number;
    recordings: number;
  };
}

export interface CommandResponse {
  request_id: string;
  status: string;
}

export function getStatus(): Promise<BackendStatus> {
  return request<BackendStatus>("/api/status");
}

export async function getCanIds(): Promise<CanMessageDef[]> {
  const payload = await request<{ ids: CanMessageDef[] }>("/api/can/ids");
  return payload.ids;
}

export async function getFrames(limit = 300): Promise<CanFrame[]> {
  const payload = await request<{ frames: CanFrame[] }>(`/api/can/frames?limit=${limit}`);
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
  bus?: string;
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
  bus?: string;
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

export function stopPeriodic(id: string): Promise<CommandResponse> {
  return request<CommandResponse>("/api/cmd/periodic", {
    method: "POST",
    body: JSON.stringify({ action: "stop", id })
  });
}
