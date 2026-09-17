import { test } from '@playwright/test'

test('capture dashboard viewport screenshot', async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 900 })
  await page.goto('/dashboard')
  await page.waitForSelector('[data-testid="dashboard-hud"]')
  // Click Preview button if present to showcase active dials & telemetry
  const previewBtn = page.getByTestId('toggle-demo-mode')
  if (await previewBtn.isVisible()) {
    await previewBtn.click()
    await page.waitForTimeout(500)
  }

  // Capture viewport screenshot (NOT full page, exactly what user sees)
  await page.screenshot({
    path: 'C:/Users/logsh/.gemini/antigravity/brain/ec70d578-7193-4ed4-ae74-124661cd2023/dashboard_fitted_1440x900.png',
    fullPage: false,
  })

  // Also check 1366x768 common laptop resolution
  await page.setViewportSize({ width: 1366, height: 768 })
  await page.waitForTimeout(1000)
  await page.screenshot({
    path: 'C:/Users/logsh/.gemini/antigravity/brain/ec70d578-7193-4ed4-ae74-124661cd2023/dashboard_fitted_1366x768.png',
    fullPage: false,
  })
})
