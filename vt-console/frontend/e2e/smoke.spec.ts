import { test, expect } from '@playwright/test'

// One basic smoke test per workplan Phase 4 scope: boot the real backend
// (Pure Software profile), load the frontend, and confirm the WS stream
// connects and the header reflects live status. Full per-workspace E2E
// coverage (overview/network/live-can specs) is deferred — see workplan.md.
test('connects to the backend and shows a live stream', async ({ page }) => {
  await page.goto('/')
  await expect(page.getByText('VTC')).toBeVisible()
  await expect(page.getByText('live', { exact: true })).toBeVisible({ timeout: 10_000 })
  await expect(page.getByText('pure_software')).toBeVisible()
})
