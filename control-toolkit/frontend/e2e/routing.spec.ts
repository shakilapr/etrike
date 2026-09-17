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
})
