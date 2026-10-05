import { test, expect } from '@playwright/test'

test.describe('Dashboard Cluster vs Audit Scrollability', () => {
  test('dashboard cluster is not scrollable, but audit page from it is scrollable', async ({ page }) => {
    await page.setViewportSize({ width: 1280, height: 720 })
    await page.goto('/dashboard')
    await page.waitForSelector('[data-testid="dashboard-hud"]')

    const main = page.locator('main')
    const container = page.locator('.dashboard-container')

    // 1. In Cluster view, dashboard must NOT be scrollable
    await expect(main).toHaveClass(/overflow-hidden/)
    await expect(container).toHaveClass(/is-cluster-view/)

    // 2. Click the Protocol Status badge to navigate to the audit page
    const protocolBadge = page.getByTestId('dashboard-protocol-summary')
    await expect(protocolBadge).toBeVisible()
    await protocolBadge.click()

    // 3. Now in tables/audit view: main must be scrollable
    await expect(container).toHaveClass(/is-tables-view/)
    await expect(main).toHaveClass(/overflow-auto/)
    await expect(page.getByTestId('dashboard-summary-table')).toBeVisible()
    await expect(page.getByTestId('protocol-audit-table')).toBeVisible()

    // Verify main scrollHeight is greater than clientHeight
    const isScrollable = await main.evaluate((el) => el.scrollHeight > el.clientHeight)
    expect(isScrollable).toBe(true)

    // Verify scrolling works
    await main.evaluate((el) => el.scrollTo({ top: 300 }))
    const scrollTop = await main.evaluate((el) => el.scrollTop)
    expect(scrollTop).toBeGreaterThan(0)

    // 4. Test Activation Gates audit tab
    await page.getByTestId('tab-activation-gates').click()
    await expect(page.getByTestId('activation-gates-full-table')).toBeVisible()
    const gatesScrollable = await main.evaluate((el) => el.scrollHeight > el.clientHeight)
    expect(gatesScrollable).toBe(true)

    // 5. Switch back to Cluster view: must be non-scrollable and scrolled to top
    await page.getByTestId('toggle-view-cluster').click()
    await expect(container).toHaveClass(/is-cluster-view/)
    await expect(main).toHaveClass(/overflow-hidden/)
    const clusterScrollTop = await main.evaluate((el) => el.scrollTop)
    expect(clusterScrollTop).toBe(0)
  })
})
