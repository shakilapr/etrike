/**
 * E2E tests for the CAN Injector workspace (dense layout).
 */
import { test, expect } from '@playwright/test'

test.describe('CAN Injector Workspace', () => {
  test('loads CAN Injector workspace with dense panels', async ({ page }) => {
    await page.goto('/')
    await page.getByTestId('nav-inject').click()
    await expect(page.getByTestId('workspace-inject')).toBeVisible()

    // Toolbar + mode
    await expect(page.getByTestId('inject-gate')).toBeVisible()
    await expect(page.getByTestId('inject-mode-named')).toBeVisible()
    await expect(page.getByTestId('inject-mode-raw')).toBeVisible()
    await expect(page.getByTestId('inject-preview-hex')).toBeVisible()

    // Named editor + Active TX rail
    await expect(page.getByTestId('inject-named-panel')).toBeVisible()
    await expect(page.getByTestId('inject-bus-tabs')).toBeVisible()
    await expect(page.getByTestId('inject-message')).toBeVisible()
    await expect(page.getByTestId('inject-side-manager')).toBeVisible()
    await expect(page.getByTestId('inject-active-jobs')).toBeVisible()

    // Templates open by default when no actives
    await expect(page.getByTestId('inject-templates')).toBeVisible()
    await expect(page.getByTestId('inject-ack-log')).toBeVisible()
  })

  test('switches buses and loads templates', async ({ page }) => {
    await page.goto('/')
    await page.getByTestId('nav-inject').click()

    await page.getByTestId('inject-bus-low').click()
    await expect(page.getByTestId('inject-bus-low')).toHaveClass(/active/)

    // Ensure templates section open
    const templates = page.getByTestId('inject-templates')
    if (!(await templates.isVisible())) {
      await page.getByTestId('inject-templates-toggle').click()
    }

    await page.getByTestId('inject-template-host-drive-fwd').click()
    await expect(page.getByTestId('inject-bus-high')).toHaveClass(/active/)
    await expect(page.getByTestId('inject-preview-hex')).not.toHaveText('(empty)')
  })

  test('switches to raw inject mode', async ({ page }) => {
    await page.goto('/')
    await page.getByTestId('nav-inject').click()

    await page.getByTestId('inject-mode-raw').click()
    await expect(page.getByTestId('inject-raw-panel')).toBeVisible()
    await expect(page.getByTestId('raw-can-id')).toBeVisible()
    await expect(page.getByTestId('raw-data-hex')).toBeVisible()
    await expect(page.getByTestId('raw-submit')).toBeVisible()
    // Active rail still present in raw mode
    await expect(page.getByTestId('inject-side-manager')).toBeVisible()
  })

  test('transmit log is collapsible', async ({ page }) => {
    await page.goto('/')
    await page.getByTestId('nav-inject').click()
    await expect(page.getByTestId('inject-log-toggle')).toBeVisible()
    // collapsed by default — table not required
    await page.getByTestId('inject-log-toggle').click()
    await expect(page.getByTestId('inject-ack-log')).toContainText(/No transmissions|Time/)
  })
})
