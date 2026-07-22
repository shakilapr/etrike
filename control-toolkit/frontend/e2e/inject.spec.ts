/**
 * E2E tests for the CAN Injector workspace.
 */
import { test, expect } from '@playwright/test'

test.describe('CAN Injector Workspace', () => {
  test('loads CAN Injector workspace with all panels', async ({ page }) => {
    await page.goto('/')
    await page.getByTestId('nav-inject').click()
    await expect(page.getByTestId('workspace-inject')).toBeVisible()

    // Gate & mode controls
    await expect(page.getByTestId('inject-gate')).toBeVisible()
    await expect(page.getByTestId('inject-mode-named')).toBeVisible()
    await expect(page.getByTestId('inject-mode-raw')).toBeVisible()

    // Named injection main & side panels
    await expect(page.getByTestId('inject-named-panel')).toBeVisible()
    await expect(page.getByTestId('inject-bus-tabs')).toBeVisible()
    await expect(page.getByTestId('inject-message')).toBeVisible()

    // Started controllers & templates side panel
    await expect(page.getByTestId('inject-side-manager')).toBeVisible()
    await expect(page.getByTestId('inject-templates')).toBeVisible()

    // Active periodic jobs and ACK log
    await expect(page.getByTestId('inject-active-jobs')).toBeVisible()
    await expect(page.getByTestId('inject-ack-log')).toBeVisible()
  })

  test('switches buses and loads templates', async ({ page }) => {
    await page.goto('/')
    await page.getByTestId('nav-inject').click()

    // Switch bus to LOW
    await page.getByTestId('inject-bus-low').click()
    await expect(page.getByTestId('inject-bus-low')).toHaveClass(/active/)

    // Click template: Host drive forward
    await page.getByTestId('inject-template-host-drive-fwd').click()
    await expect(page.getByTestId('inject-bus-high')).toHaveClass(/active/)

    // Verify wire preview hex updates
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
  })
})
