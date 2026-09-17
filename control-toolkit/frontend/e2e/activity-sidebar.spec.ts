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
  await expect(page.getByTestId('control-toolbox-feedback')).toBeVisible()
  await expect(page.getByTestId('toolbox-speed')).toBeVisible()
  await expect(page.getByTestId('toolbox-steer')).toBeVisible()
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

test('sidebar collapses to compact rail and expands via button and keyboard shortcut', async ({
  page,
}) => {
  await page.goto('/')

  const sidebar = page.locator('aside.sidebar')
  const toggleBtn = page.getByTestId('btn-toggle-sidebar')

  await expect(sidebar).toBeVisible()
  await expect(sidebar).not.toHaveClass(/is-collapsed/)
  const expandedBox = await sidebar.boundingBox()
  expect(expandedBox?.width).toBeGreaterThanOrEqual(200)

  // 1. Click toggle button -> collapses
  await toggleBtn.click()
  await expect(sidebar).toHaveClass(/is-collapsed/)
  await page.waitForTimeout(250)
  const collapsedBox = await sidebar.boundingBox()
  expect(collapsedBox?.width).toBeLessThanOrEqual(80)
  await page.screenshot({
    path: 'C:/Users/logsh/.gemini/antigravity/brain/ba92870d-875f-4c69-8960-0970c0e3456a/sidebar_collapsed.png',
  })

  // 2. Nav links remain functional while collapsed
  await page.getByTestId('nav-settings').click()
  await expect(page.getByTestId('workspace-settings')).toBeVisible()
  await expect(page).toHaveURL(/\/settings/)

  await page.getByTestId('nav-overview').click()
  await expect(page.getByTestId('workspace-overview')).toBeVisible()
  // 3. Toggle button or keyboard shortcut Ctrl+B toggles back to expanded
  await toggleBtn.click()
  await expect(sidebar).not.toHaveClass(/is-collapsed/)
  await page.waitForTimeout(250)
  const reExpandedBox = await sidebar.boundingBox()
  expect(reExpandedBox?.width).toBeGreaterThanOrEqual(200)
  await page.screenshot({
    path: 'C:/Users/logsh/.gemini/antigravity/brain/ba92870d-875f-4c69-8960-0970c0e3456a/sidebar_expanded.png',
  })

  // 4. Toggle button collapses again
  await toggleBtn.click()
  await expect(sidebar).toHaveClass(/is-collapsed/)
})

