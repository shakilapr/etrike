import { expect, test } from '@playwright/test'

test('activity rail swaps contextual sidebars only (main workspace unchanged)', async ({ page }) => {
  await page.goto('/')

  await expect(page.getByTestId('activity-bar')).toBeVisible()
  await expect(page.getByTestId('sidebar')).toBeVisible()
  await expect(page.getByTestId('workspace-overview')).toBeVisible()

  // Control activity: operate tools only — does not open Control workspace.
  await page.getByTestId('activity-control').click()
  await expect(page.getByTestId('sidebar-control')).toBeVisible()
  await expect(page.getByTestId('workspace-overview')).toBeVisible()
  await expect(page.getByTestId('workspace-control')).toHaveCount(0)
  await expect(page.getByTestId('sidebar-bench-toggle')).toBeVisible()
  await expect(page.getByTestId('control-keyboard')).toBeVisible()
  await expect(page.getByTestId('sidebar-kb-toggle')).toBeVisible()
  await expect(page.getByTestId('control-fake-signals')).toBeVisible()
  await expect(page.getByTestId('sidebar-stop-all')).toBeVisible()
  const stopAllBox = await page.getByTestId('sidebar-stop-all').boundingBox()
  expect(stopAllBox?.height).toBeGreaterThanOrEqual(40)
  await expect(page.getByTestId('control-route-high')).toHaveCount(0)

  // Monitor activity: simplified live CAN only; main stays put.
  await page.getByTestId('activity-monitor').click()
  await expect(page.getByTestId('sidebar-monitor')).toBeVisible()
  await expect(page.getByTestId('monitor-live-simplified')).toBeVisible()
  await expect(page.getByTestId('monitor-bus-both')).toBeVisible()
  await expect(page.getByTestId('workspace-overview')).toBeVisible()
  await expect(page.getByTestId('workspace-live')).toHaveCount(0)

  // Workspace explorer is the only activity that changes main workspace.
  await page.getByTestId('activity-explorer').click()
  await expect(page.getByTestId('sidebar')).toBeVisible()
  await page.getByTestId('nav-control').click()
  await expect(page.getByTestId('workspace-control')).toBeVisible()
})
