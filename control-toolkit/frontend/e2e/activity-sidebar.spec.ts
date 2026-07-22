import { expect, test } from '@playwright/test'

test('activity rail swaps contextual sidebars only (main workspace unchanged)', async ({ page }) => {
  await page.goto('/')

  await expect(page.getByTestId('activity-bar')).toBeVisible()
  await expect(page.getByTestId('sidebar')).toBeVisible()
  await expect(page.getByTestId('workspace-overview')).toBeVisible()

  // Activity bar must not navigate the main pane.
  await page.getByTestId('activity-control').click()
  await expect(page.getByTestId('sidebar-control')).toBeVisible()
  await expect(page.getByTestId('workspace-overview')).toBeVisible()
  await expect(page.getByTestId('workspace-control')).toHaveCount(0)
  await expect(page.getByTestId('control-route-high')).toHaveClass(/active/)
  await expect(page.getByTestId('sidebar-bench-toggle')).toBeVisible()

  // Choosing a control route still opens Control in the main window.
  await page.getByTestId('control-route-mtr').click()
  await expect(page.getByTestId('control-route-mtr')).toHaveClass(/active/)
  await expect(page.getByTestId('workspace-control')).toBeVisible()
  await expect(page.getByTestId('direct-mtr')).toBeVisible()
  await expect(page.getByTestId('direct-motor')).toBeVisible()
  await expect(page.getByTestId('direct-steering')).toHaveCount(0)
  await expect(page.getByTestId('direct-brake')).toHaveCount(0)

  // Monitor activity: sidebar only; main stays on Control until a nav pick.
  await page.getByTestId('activity-monitor').click()
  await expect(page.getByTestId('sidebar-monitor')).toBeVisible()
  await expect(page.getByTestId('workspace-control')).toBeVisible()
  await expect(page.getByTestId('workspace-live')).toHaveCount(0)

  await page.getByTestId('activity-explorer').click()
  await expect(page.getByTestId('sidebar')).toBeVisible()
  await expect(page.getByTestId('workspace-control')).toBeVisible()
})
