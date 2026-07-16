const BASE = '/api/v1'

async function json<T>(path: string, init?: RequestInit): Promise<T> {
  const r = await fetch(`${BASE}${path}`, {
    headers: { 'Content-Type': 'application/json', ...(init?.headers || {}) },
    ...init,
  })
  if (!r.ok) {
    const body = await r.json().catch(() => ({}))
    throw new Error(body.detail || body.title || r.statusText)
  }
  return r.json() as Promise<T>
}

export const api = {
  status: () => json<import('./store').Status>('/status'),
  state: () =>
    json<{ sequence: number; messages: import('./store').MessageState[] }>('/state'),
  createSession: (profile = 'pure_software') =>
    json<{ session: import('./store').SessionState }>('/sessions', {
      method: 'POST',
      body: JSON.stringify({ profile }),
    }),
  setBenchTx: (sessionId: string, enabled: boolean, expected_revision: number) =>
    json<{ session: import('./store').SessionState }>(
      `/sessions/${sessionId}/bench-tx`,
      {
        method: 'POST',
        body: JSON.stringify({ enabled, expected_revision }),
      },
    ),
  stopAll: (sessionId: string, expected_revision: number) =>
    json<{ session: import('./store').SessionState }>(
      `/sessions/${sessionId}/stop-all`,
      {
        method: 'POST',
        body: JSON.stringify({ expected_revision }),
      },
    ),
  inject: (body: Record<string, unknown>) =>
    json<Record<string, unknown>>('/injections', {
      method: 'POST',
      body: JSON.stringify(body),
    }),
  startPeers: (names?: string[]) =>
    json<Record<string, unknown>>('/synthetic-peers/start', {
      method: 'POST',
      body: JSON.stringify({ names: names ?? null }),
    }),
}
