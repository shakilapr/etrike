import type { APIRequestContext } from '@playwright/test'

/**
 * Return the shared backend to a clean, receive-only Computer session.
 *
 * Several full-stack tests intentionally latch ESTOP. Closing the session is
 * the product-supported reset boundary; creating a new Pure Software session
 * then restores a deterministic virtual transport without enabling Bench TX.
 */
export async function resetComputerSession(request: APIRequestContext) {
  const current = await request.get('/api/v1/sessions', { failOnStatusCode: true })
  const body = (await current.json()) as {
    session?: { session_id?: string | null }
  }
  const sessionId = body.session?.session_id
  if (sessionId) {
    await request.delete(`/api/v1/sessions/${sessionId}`, {
      failOnStatusCode: true,
    })
  }
  await request.post('/api/v1/sessions', {
    data: { profile: 'pure_software' },
    failOnStatusCode: true,
  })
}
