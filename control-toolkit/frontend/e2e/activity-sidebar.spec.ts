import { expect, test } from '@playwright/test'

test('activity rail swaps contextual sidebars and control routes', async ({ page }) => {
  await page.goto('/')

  await expect(page.getByTestId('activity-bar')).toBeVisible()
  await expect(page.getByTestId('sidebar')).toBeVisible()

  await page.getByTestId('activity-control').click()
  await expect(page.getByTestId('sidebar-control')).toBeVisible()
  await expect(page.getByTestId('workspace-control')).toBeVisible()
  await expect(page.getByTestId('control-route-high')).toHaveClass(/active/)
  await expect(page.getByTestId('sidebar-bench-toggle')).toBeVisible()

  await page.getByTestId('control-route-mtr').click()
  await expect(page.getByTestId('control-route-mtr')).toHaveClass(/active/)
  await expect(page.getByTestId('direct-mtr')).toBeVisible()
  await expect(page.getByTestId('direct-motor')).toBeVisible()
  await expect(page.getByTestId('direct-steering')).toHaveCount(0)
  await expect(page.getByTestId('direct-brake')).toHaveCount(0)

  await page.getByTestId('activity-monitor').click()
  await expect(page.getByTestId('sidebar-monitor')).toBeVisible()
  await expect(page.getByTestId('workspace-live')).toBeVisible()

  await page.getByTestId('activity-explorer').click()
  await expect(page.getByTestId('sidebar')).toBeVisible()
  await expect(page.getByTestId('workspace-overview')).toBeVisible()
})
