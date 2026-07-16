import { test, expect } from '@playwright/test'

test.describe('Control Toolkit UI (Pure Software)', () => {
  test('loads shell and shows stream/live status', async ({ page }) => {
    await page.goto('/')
    await expect(page.getByTestId('app')).toBeVisible()
    await expect(page.getByTestId('topbar')).toBeVisible()
    await expect(page.getByTestId('workspace-overview')).toBeVisible()
    await expect(page.getByTestId('safety-strip')).toBeVisible()
    await expect(page.getByTestId('sidebar')).toBeVisible()

    // Full nav rail from architecture §6.2 + vehicle preview
    for (const id of [
      'overview',
      'network',
      'live',
      'control',
      'preview',
      'bench',
      'dictionary',
      'diagnostics',
      'settings',
    ]) {
      await expect(page.getByTestId(`nav-${id}`)).toBeVisible()
    }

    // Stream should leave connecting state once backend is up.
    await expect(page.getByTestId('chip-stream')).not.toHaveText(/CONNECTING/i, {
      timeout: 20_000,
    })
    await expect(page.getByTestId('chip-stream')).toContainText(/Live|Delayed|Lost|Connecting/i)
    await expect(page.getByTestId('chip-profile')).toContainText(
      /Pure Software|pure_software|—/i,
    )
  })

  test('navigates workspaces', async ({ page }) => {
    await page.goto('/')

    await page.getByTestId('nav-network').click()
    await expect(page.getByTestId('workspace-network')).toBeVisible()
    await expect(page.getByTestId('topology-map')).toBeVisible()

    await page.getByTestId('nav-live').click()
    await expect(page.getByTestId('workspace-live')).toBeVisible()
    await expect(page.getByTestId('live-can-table')).toBeVisible()
    await expect(page.getByTestId('live-filter')).toBeVisible()

    await page.getByTestId('nav-control').click()
    await expect(page.getByTestId('workspace-control')).toBeVisible()
    await expect(page.getByTestId('input-yaw')).toBeVisible()
    await expect(page.getByTestId('btn-inject-drive')).toBeVisible()

    await page.getByTestId('nav-preview').click()
    await expect(page.getByTestId('workspace-preview')).toBeVisible()
    await expect(page.getByTestId('preview-canvas')).toBeVisible()
    await expect(page.getByTestId('preview-telemetry')).toBeVisible()
    await expect(page.getByTestId('drive-keycaps')).toBeVisible()
    await expect(page.getByTestId('btn-drive-arm')).toBeVisible()
    await expect(page.getByTestId('preview-mode-adaptive')).toBeVisible()
    await expect(page.getByTestId('preview-mode-direct')).toBeVisible()

    await page.getByTestId('nav-bench').click()
    await expect(page.getByTestId('workspace-bench')).toBeVisible()

    await page.getByTestId('nav-dictionary').click()
    await expect(page.getByTestId('workspace-dictionary')).toBeVisible()

    await page.getByTestId('nav-diagnostics').click()
    await expect(page.getByTestId('workspace-diagnostics')).toBeVisible()

    await page.getByTestId('nav-settings').click()
    await expect(page.getByTestId('workspace-settings')).toBeVisible()
    await expect(page.getByTestId('profile-list')).toBeVisible()
  })

  test('analysis inject: yaw/speed appear on Overview and Live CAN', async ({
    page,
  }) => {
    await page.goto('/')
    await page.getByTestId('nav-control').click()

    await page.getByTestId('input-speed').fill('800')
    await page.getByTestId('input-yaw').fill('420')
    await page.getByTestId('input-gear').fill('1')
    await page.getByTestId('check-periodic').uncheck()

    await page.getByTestId('btn-inject-drive').click()

    await expect(page.getByTestId('control-log')).toContainText(/host-drive|oneshot|submitted/i, {
      timeout: 15_000,
    })

    // Overview metrics
    await page.getByTestId('nav-overview').click()
    await expect(page.getByTestId('metric-yaw')).toContainText('420', {
      timeout: 15_000,
    })
    await expect(page.getByTestId('metric-speed')).toContainText('800')

    // Live table row for 0x300
    await page.getByTestId('nav-live').click()
    await expect(page.getByTestId('live-can-table')).toContainText('HOST_DRIVE_CMD', {
      timeout: 10_000,
    })
    await expect(page.getByTestId('live-can-table')).toContainText(/yaw_rate_mrad_s=420/)
  })

  test('stop all clears control path without crashing UI', async ({ page }) => {
    await page.goto('/')
    await page.getByTestId('nav-control').click()
    await page.getByTestId('btn-enable-tx').click()
    await expect(page.getByTestId('control-log')).toContainText(/Bench TX enabled/i, {
      timeout: 10_000,
    })
    await page.getByTestId('btn-stop-all').click()
    await expect(page.getByTestId('control-log')).toContainText(/Stop All/i, {
      timeout: 10_000,
    })
    await expect(page.getByTestId('app')).toBeVisible()
  })

  test('settings lists profiles and Pure Software starts', async ({ page }) => {
    await page.goto('/')
    await page.getByTestId('nav-settings').click()
    await expect(page.getByTestId('profile-list')).toContainText(/Pure Software/i)
    await expect(page.getByTestId('profile-list')).toContainText(/Bench Test|Full Vehicle/i)
    await page.getByTestId('btn-start-pure').click()
    await expect(page.getByTestId('settings-log')).toContainText(/Session ses_|phase running/i, {
      timeout: 10_000,
    })
  })

  test('drive console arms CAN control and shows keycaps', async ({ page }) => {
    await page.goto('/')
    await page.getByTestId('nav-preview').click()
    await page.getByTestId('btn-drive-arm').click()
    await expect(page.getByTestId('drive-log')).toContainText(/Armed|HOST_DRIVE|CAN/i, {
      timeout: 15_000,
    })
    await expect(page.getByTestId('btn-drive-disarm')).toBeVisible()
    await expect(page.getByTestId('drive-gauges')).toBeVisible()
    await page.getByTestId('preview-mode-direct').click()
    await expect(page.getByTestId('preview-mode-blurb')).toContainText(/Direct/i)
  })

  test('drive arm + W key publishes HOST_DRIVE_CMD on Live CAN', async ({ page }) => {
    await page.goto('/')
    await page.getByTestId('nav-preview').click()
    await page.getByTestId('preview-canvas-wrap').click()
    await page.getByTestId('btn-drive-arm').click()
    await expect(page.getByTestId('btn-drive-disarm')).toBeVisible({ timeout: 15_000 })

    await page.getByTestId('preview-canvas-wrap').focus()
    await page.keyboard.down('KeyW')
    await page.waitForTimeout(400)
    await page.keyboard.up('KeyW')

    await page.getByTestId('nav-live').click()
    await expect(page.getByTestId('live-can-table')).toContainText('HOST_DRIVE_CMD', {
      timeout: 15_000,
    })
  })

  test('direct actuator motor stream from Control', async ({ page }) => {
    await page.goto('/')
    await page.getByTestId('nav-control').click()
    await page.getByTestId('direct-motor-speed').fill('350')
    await page.getByTestId('btn-direct-motor-start').click()
    await expect(page.getByTestId('control-log')).toContainText(/Direct motor|motor/i, {
      timeout: 15_000,
    })
    await page.getByTestId('nav-live').click()
    await expect(page.getByTestId('live-can-table')).toContainText(/RT_DRIVE_CMD|0x204/i, {
      timeout: 15_000,
    })
  })

  test('live CAN stream view toggles chronological history', async ({ page }) => {
    await page.goto('/')
    await page.getByTestId('nav-live').click()
    await page.getByTestId('live-mode-chrono').click()
    await expect(page.getByTestId('live-chrono-table')).toBeVisible()
    await expect(page.getByTestId('live-pause')).toBeVisible()
  })
})
