import { expect, test } from '@playwright/test'
import { resetComputerSession } from './session-reset'

test.describe('Computer / Real mode and managed simulation', () => {
  test.beforeEach(async ({ request }) => {
    await resetComputerSession(request)
  })

  test.afterEach(async ({ request }) => {
    await request.post('/api/v1/simulation/start').catch(() => undefined)
    await resetComputerSession(request)
  })

  test('Computer exposes truthful runtime indicators and working RT SIL start/stop', async ({ page, request }) => {
    await page.goto('/')
    await page.getByTestId('nav-settings').click()
    const runtime = page.getByTestId('software-runtime-panel')
    await expect(runtime).toBeVisible()
    await expect(runtime).toContainText('RT SIL')
    await expect(runtime).toContainText('SYS SIL')
    await expect(page.getByTestId('simulation-scope')).toContainText(/not full RT tasks/i)
    await expect(page.getByTestId('simulation-scope')).toContainText(/Managed in-process SYS|SYS_HEARTBEAT|SYS peer/i)
    await expect(runtime).toContainText(/SYS SIL\s*running/i)

    await page.getByTestId('btn-simulation-stop').click()
    await expect(runtime).toContainText(/RT SIL\s*stopped/i)
    await expect(runtime).toContainText(/SYS SIL\s*stopped/i)
    let sim = (await (await request.get('/api/v1/simulation')).json()).simulation
    expect(sim.rt_sil.state).toBe('stopped')
    expect(sim.sys_sil.state).toBe('stopped')
    expect(sim.virtual_can.state).toBe('running')

    await page.getByTestId('btn-simulation-start').click()
    await expect(runtime).toContainText(/SYS SIL\s*running/i)
    sim = (await (await request.get('/api/v1/simulation')).json()).simulation
    expect(sim.sys_sil.state).toBe('running')
    // RT runs only when native executable is configured for the e2e API process.
    if (sim.rt_sil.available) {
      await expect(runtime).toContainText(/RT SIL\s*running/i)
      expect(sim.rt_sil.state).toBe('running')
    }
  })

  test('unavailable Real activation explains failure and preserves Computer', async ({ page }) => {
    await page.goto('/')
    await page.getByTestId('nav-settings').click()
    await expect(page.getByTestId('mode-real')).toBeEnabled()
    await page.getByTestId('mode-real').click()
    await expect(page.getByTestId('settings-log')).toContainText(/CANalyst-II USB device|not found|unavailable/i)
    await expect(page.getByTestId('settings-active-mode')).toContainText('Computer')
    await expect(page.getByTestId('software-runtime-panel')).toBeVisible()

    await page.getByTestId('topbar-mode-real').click()
    await expect(page.getByTestId('topbar-action-error')).toContainText(/CANalyst-II USB device|not found|unavailable/i)
    await expect(page.getByTestId('topbar-mode-computer')).toHaveAttribute('aria-pressed', 'true')
  })

  test('Computer ESTOP explicitly establishes virtual TX prerequisites and reports success', async ({ page, request }) => {
    await page.goto('/')
    const before = await (await request.get('/api/v1/status')).json()
    expect(before.session.bench_tx).toBe('disabled')

    await page.getByTestId('btn-header-estop').click()
    await expect(page.getByTestId('chip-estop')).toHaveClass(/danger/)
    await expect(page.getByTestId('chip-estop-label')).not.toHaveText('Clear')
    await expect.poll(async () => {
      const after = await (await request.get('/api/v1/status')).json()
      return {
        profile: after.session.profile,
        bench_tx: after.session.bench_tx,
        estop_active: after.session.estop_active,
      }
    }).toEqual({ profile: 'pure_software', bench_tx: 'enabled', estop_active: true })
    await expect.poll(async () => {
      const body = await (
        await request.get('/api/v1/logs?category=safety&limit=20')
      ).json()
      return body.logs?.some(
        (entry: { code?: string; data?: { origin?: string } }) =>
          entry.code === 'safety.estop_frame' && entry.data?.origin === 'host_toolkit',
      )
    }).toBe(true)
  })

  test('control actions do not silently create a Computer session', async ({ page, request }) => {
    await resetComputerSession(request)
    const current = await (await request.get('/api/v1/status')).json()
    await request.delete(`/api/v1/sessions/${current.session.session_id}`)

    await page.goto('/')
    await page.getByTestId('nav-control').click()
    await expect(page.getByTestId('btn-open-settings-session')).toBeVisible()
    await page.getByTestId('btn-inject-drive').click()
    await expect(page.getByTestId('control-log')).toContainText(/No active session/i)
    const after = await (await request.get('/api/v1/status')).json()
    expect(after.session.session_id).toBeNull()
    expect(after.session.phase).toBe('stopped')
  })

  test('numeric drafts allow clear and negative entry without forcing zero', async ({ page }) => {
    await page.goto('/')
    await page.getByTestId('nav-control').click()
    const speed = page.getByTestId('input-speed')
    await speed.fill('')
    await expect(speed).toHaveValue('')
    await speed.fill('-')
    await expect(speed).toHaveValue('-')
    await speed.fill('-500')
    await expect(speed).toHaveValue('-500')
    await speed.blur()
    await expect(speed).toHaveValue('-500')
  })
})
