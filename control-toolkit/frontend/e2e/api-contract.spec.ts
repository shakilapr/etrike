import { expect, test } from '@playwright/test'

test.describe('Control Toolkit API-integrated contract coverage', () => {
  test('all read-only workspaces have live backend data', async ({ request }) => {
    const endpoints = [
      '/api/v1/status', '/api/v1/state', '/api/v1/history?limit=5',
      '/api/v1/topology', '/api/v1/settings', '/api/v1/sessions',
      '/api/v1/sessions/profiles', '/api/v1/control/status',
      '/api/v1/protocol/messages', '/api/v1/protocol/dictionary',
      '/api/v1/events?limit=5', '/api/v1/episodes', '/api/v1/recordings',
      '/api/v1/logs?limit=5', '/api/v1/logs/stats', '/api/v1/synthetic-peers',
      '/api/v1/tests',
    ]
    for (const endpoint of endpoints) {
      const response = await request.get(endpoint)
      expect(response.status(), endpoint).toBe(200)
      expect(await response.json(), endpoint).toBeTruthy()
    }
  })

  test('session, lease, TX gate, control, HMI, diagnostics and recording flow', async ({ request }) => {
    const create = await request.post('/api/v1/sessions', { data: { profile: 'pure_software' } })
    expect(create.status()).toBe(200)
    const session = (await create.json()).session
    const sid = session.session_id

    const lease = await request.post(`/api/v1/sessions/${sid}/leases`, {
      data: { bus: 'high', can_id: 0x300, owner: 'playwright', resource: 'host-drive' },
    })
    expect(lease.status()).toBe(200)
    const leaseBody = await lease.json()
    expect((await request.post(`/api/v1/sessions/${sid}/leases/renew`, {
      data: { lease_id: leaseBody.lease_id },
    })).status()).toBe(200)

    expect((await request.post(`/api/v1/sessions/${sid}/bench-tx`, { data: { enabled: true } })).status()).toBe(200)
    expect((await request.post('/api/v1/control/intent', {
      data: { speed_mmps: 500, yaw_rate_mrad_s: 20, gear: 1, source: 'playwright', sequence: 1 },
    })).status()).toBe(200)
    expect((await request.post('/api/v1/hmi/mode', { data: { req_mode: 1, enabled: true } })).status()).toBe(200)
    expect((await request.post('/api/v1/hmi/power', { data: { req_start: 1, enabled: true } })).status()).toBe(200)

    const recording = await request.post('/api/v1/recordings')
    expect(recording.status()).toBe(200)
    const rid = (await recording.json()).recording.recording_id
    expect((await request.delete(`/api/v1/recordings/${rid}`)).status()).toBe(200)
    expect((await request.get(`/api/v1/recordings/${rid}/export`)).status()).toBe(200)
    expect((await request.get(`/api/v1/recordings/${rid}/export/vector`)).headers()['content-type']).toContain('application/zip')

    expect((await request.post(`/api/v1/sessions/${sid}/stop-all`)).status()).toBe(200)
    expect((await request.delete(`/api/v1/sessions/${sid}/leases/${leaseBody.lease_id}`)).status()).toBe(200)
    expect((await request.delete(`/api/v1/sessions/${sid}`)).status()).toBe(200)
  })

  test('invalid and unsafe operations are rejected by the backend', async ({ request }) => {
    expect((await request.get('/api/v1/protocol/messages/high/not-an-id')).status()).toBe(400)
    expect((await request.get('/api/v1/evidence/missing?limit=0')).status()).toBe(422)
    expect((await request.get('/api/v1/recordings/missing/export/vector')).status()).toBe(404)
    expect((await request.post('/api/v1/sessions', { data: { profile: 'invalid-profile' } })).status()).toBeGreaterThanOrEqual(400)
    expect((await request.post('/api/v1/control/direct', { data: { actuator: 'unknown' } })).status()).toBeGreaterThanOrEqual(400)
  })
})
