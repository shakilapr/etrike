import { test } from '@playwright/test'

test('capture live can viewport and bus health strip', async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 900 })
  await page.goto('http://127.0.0.1:5173/live')
  await page.waitForSelector('[data-testid="live-can-bus-health-strip"]')
  // Wait for live dummy CAN stream to populate over SSE/WebSocket
  await page.waitForTimeout(3000)

  // Capture bus health strip element screenshot
  const strip = page.getByTestId('live-can-bus-health-strip')
  await strip.screenshot({
    path: 'C:/Users/logsh/.gemini/antigravity/brain/30df57d0-d59a-452c-ad71-0ffc9486edde/bus_health_strip_1440.png',
  })

  // Capture full viewport
  await page.screenshot({
    path: 'C:/Users/logsh/.gemini/antigravity/brain/30df57d0-d59a-452c-ad71-0ffc9486edde/live_can_1440x900.png',
    fullPage: false,
  })

  // 1366x768 common laptop
  await page.setViewportSize({ width: 1366, height: 768 })
  await page.waitForTimeout(1000)
  await page.screenshot({
    path: 'C:/Users/logsh/.gemini/antigravity/brain/30df57d0-d59a-452c-ad71-0ffc9486edde/live_can_1366x768.png',
    fullPage: false,
  })
})
