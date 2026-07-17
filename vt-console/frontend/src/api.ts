import type { ProtocolMessagesResponse, SessionState, StatusResponse } from './types'

const BASE = '/api/v1'

async function getJson<T>(path: string): Promise<T> {
  const r = await fetch(`${BASE}${path}`)
  if (!r.ok) throw new Error(`${path} -> ${r.status}`)
  return (await r.json()) as T
}

export const api = {
  status: () => getJson<StatusResponse>('/status'),
  protocolMessages: () => getJson<ProtocolMessagesResponse>('/protocol/messages'),
  sessions: () => getJson<{ session: SessionState }>('/sessions'),
}
