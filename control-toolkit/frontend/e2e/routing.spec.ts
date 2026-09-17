import { test, expect } from '@playwright/test'
import { resetComputerSession } from './session-reset'

test.describe('URL Routing & History Navigation', () => {
  test.beforeEach(async ({ request }) => {
    await resetComputerSession(request)
  })

  test('root path "/" defaults to /overview and sets page title', async ({ page }) => {
    await page.goto('/')
    await expect(page.getByTestId('app')).toBeVisible()
    await expect(page.getByTestId('workspace-overview')).toBeVisible()
    await expect(page.getByTestId('nav-overview')).toHaveClass(/active/)
    await expect(page).toHaveURL(/\/overview$/)
    await expect(page).toHaveTitle(/Overview/i)
    await page.screenshot({
      path: 'C:/Users/logsh/.gemini/antigravity/brain/ba92870d-875f-4c69-8960-0970c0e3456a/url_overview.png',
    })
  })

  test('direct navigation to /settings loads Settings workspace and preserves on reload', async ({ page }) => {
    await page.goto('/settings')
    await expect(page.getByTestId('app')).toBeVisible()
    await expect(page.getByTestId('nav-settings')).toHaveClass(/active/)
    await expect(page).toHaveURL(/\/settings$/)
    await expect(page).toHaveTitle(/Settings/i)
    await page.screenshot({
      path: 'C:/Users/logsh/.gemini/antigravity/brain/ba92870d-875f-4c69-8960-0970c0e3456a/url_settings.png',
    })

    // Verify page reload preserves the workspace
    await page.reload()
    await expect(page.getByTestId('nav-settings')).toHaveClass(/active/)
    await expect(page).toHaveURL(/\/settings$/)
  })

  test('direct navigation to /dashboard loads Dashboard workspace', async ({ page }) => {
    await page.goto('/dashboard')
    await expect(page.getByTestId('app')).toBeVisible()
    await expect(page.getByTestId('nav-dashboard')).toHaveClass(/active/)
    await expect(page).toHaveURL(/\/dashboard$/)
    await expect(page).toHaveTitle(/Dashboard/i)
  })

  test('sidebar clicks push corresponding URLs to browser history', async ({ page }) => {
    await page.goto('/overview')

    // Click Network
    await page.getByTestId('nav-network').click()
    await expect(page).toHaveURL(/\/network$/)
    await expect(page.getByTestId('nav-network')).toHaveClass(/active/)
    await expect(page).toHaveTitle(/Network/i)

    // Click Live CAN
    await page.getByTestId('nav-live').click()
    await expect(page).toHaveURL(/\/live$/)
    await expect(page.getByTestId('nav-live')).toHaveClass(/active/)
    await expect(page).toHaveTitle(/Live CAN/i)

    // Click Control
    await page.getByTestId('nav-control').click()
    await expect(page).toHaveURL(/\/control$/)
    await expect(page.getByTestId('nav-control')).toHaveClass(/active/)
    await expect(page).toHaveTitle(/Control/i)
  })

  test('browser Back and Forward buttons navigate workspaces properly', async ({ page }) => {
    await page.goto('/overview')
    await page.getByTestId('nav-network').click()
    await expect(page).toHaveURL(/\/network$/)

    await page.getByTestId('nav-live').click()
    await expect(page).toHaveURL(/\/live$/)

    // Back to /network
    await page.goBack()
    await expect(page).toHaveURL(/\/network$/)
    await expect(page.getByTestId('nav-network')).toHaveClass(/active/)

    // Back to /overview
    await page.goBack()
    await expect(page).toHaveURL(/\/overview$/)
    await expect(page.getByTestId('nav-overview')).toHaveClass(/active/)

    // Forward to /network
    await page.goForward()
    await expect(page).toHaveURL(/\/network$/)
    await expect(page.getByTestId('nav-network')).toHaveClass(/active/)

    // Forward to /live
    await page.goForward()
    await expect(page).toHaveURL(/\/live$/)
    await expect(page.getByTestId('nav-live')).toHaveClass(/active/)
  })

  test('alias /drive resolves to preview / drive console', async ({ page }) => {
    await page.goto('/drive')
    await expect(page.getByTestId('app')).toBeVisible()
    await expect(page.getByTestId('nav-preview')).toHaveClass(/active/)
  })

  test('top bar renders TX, Mode, Power, Inject ESTOP, Reset ESTOP icon buttons and Vehicle Telemetry', async ({ page }) => {
    await page.goto('/overview')
    const txBtn = page.getByTestId('btn-header-bench-tx')
    const modeBtn = page.getByTestId('btn-header-hmi-mode')
    const powerBtn = page.getByTestId('btn-header-hmi-power')
    const injectBtn = page.getByTestId('btn-header-estop')
    const resetBtn = page.getByTestId('btn-header-estop-reset')

    await expect(txBtn).toBeVisible()
    await expect(txBtn).toContainText(/Bench TX/i)

    await expect(modeBtn).toBeVisible()
    await expect(modeBtn).toContainText(/Mode:/i)

    await expect(powerBtn).toBeVisible()
    await expect(powerBtn).toContainText(/Power/i)

    await expect(injectBtn).toBeVisible()
    await expect(injectBtn).toContainText(/Inject ESTOP/i)

    await expect(resetBtn).toBeVisible()
    await expect(resetBtn).toContainText(/Reset ESTOP/i)

    // Verify Vehicle Telemetry cluster (Gear, Controller Modes, Power)
    const telemetryCluster = page.getByTestId('topbar-vehicle-telemetry')
    await expect(telemetryCluster).toBeVisible()
    await expect(page.getByTestId('chip-gear')).toBeVisible()
    await expect(page.getByTestId('chip-controller-modes')).toBeVisible()
    await expect(telemetryCluster.getByTestId('chip-power')).toBeVisible()

    // Capture topbar screenshot showing all 5 icon actions and dynamic telemetry cluster
    await page.locator('[data-testid="topbar"]').screenshot({
      path: 'C:/Users/logsh/.gemini/antigravity/brain/ba92870d-875f-4c69-8960-0970c0e3456a/topbar_actions_complete.png',
    })

    // Clicking buttons should execute smoothly without crashing
    await txBtn.click()
    await modeBtn.click()
    await powerBtn.click()
    await resetBtn.click()

    // Assert that action notice is shown inline in the second top bar
    const inlineNotice = page.getByTestId('topbar-action-error')
    await expect(inlineNotice).toBeVisible()

    // Capture screenshot showing the inline notice inside the second top bar
    await page.locator('[data-testid="topbar"]').screenshot({
      path: 'C:/Users/logsh/.gemini/antigravity/brain/ba92870d-875f-4c69-8960-0970c0e3456a/topbar_with_inline_notice.png',
    })
  })
})
