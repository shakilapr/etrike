/**
 * Full-system audit: every tab, major buttons, and Live CAN side-effects.
 * Fails on page errors / 5xx; collects actionable issues for control/drive/settings.
 */
import { test, expect, type Page, type ConsoleMessage } from '@playwright/test'

const NAV = [
  'overview',
  'network',
  'live',
  'control',
  'preview',
  'bench',
  'dictionary',
  'diagnostics',
  'logs',
  'settings',
] as const

async function go(page: Page, id: (typeof NAV)[number]) {
  await page.getByTestId(`nav-${id}`).click()
  await expect(page.getByTestId(`workspace-${id === 'preview' ? 'preview' : id}`)).toBeVisible({
    timeout: 12_000,
  })
}

function collectErrors(page: Page) {
  const errors: string[] = []
  const onConsole = (msg: ConsoleMessage) => {
    if (msg.type() !== 'error') return
    const t = msg.text()
    if (/Download the React DevTools|\[vite\]|favicon|WebSocket connection|status of 409/i.test(t)) {
      return
    }
    errors.push(`console: ${t.slice(0, 240)}`)
  }
  const onPageError = (err: Error) => {
    errors.push(`pageerror: ${err.message.slice(0, 240)}`)
  }
  const onResponse = (res: { url: () => string; status: () => number }) => {
    const url = res.url()
    if (!url.includes('/api/')) return
    if (res.status() >= 500) errors.push(`HTTP ${res.status()} ${url}`)
  }
  page.on('console', onConsole)
  page.on('pageerror', onPageError)
  page.on('response', onResponse)
  return {
    errors,
    detach: () => {
      page.off('console', onConsole)
      page.off('pageerror', onPageError)
      page.off('response', onResponse)
    },
  }
}

test.describe('Full system — tabs, buttons, CAN side-effects', () => {
  test.setTimeout(240_000)

  test('walk every tab, exercise controls, verify CAN updates', async ({ page }) => {
    const bag = collectErrors(page)

    await page.goto('/')
    await expect(page.getByTestId('app')).toBeVisible({ timeout: 20_000 })
    await expect(page.getByTestId('chip-stream')).not.toHaveText(/CONNECTING/i, {
      timeout: 25_000,
    })

    // ── Shell / topbar ─────────────────────────────────────────────
    for (const id of [
      'topbar',
      'sidebar',
      'health-strip',
      'chip-health-overall',
      'chip-stream',
      'chip-high',
      'chip-low',
      'btn-header-estop',
      'chip-profile',
    ]) {
      await expect(page.getByTestId(id)).toBeVisible()
    }
    // Discrete status pills on overview (not progress bars for binary)
    await expect(page.getByTestId('safety-strip')).toBeVisible()
    await expect(page.getByTestId('meter-estop')).toBeVisible()
    await expect(page.getByTestId('meter-estop')).toHaveClass(/status-pill/)
    await expect(page.getByTestId('meter-bench-tx')).toHaveClass(/status-pill/)
    await expect(page.getByTestId('meter-brake')).toBeVisible() // still a continuous meter

    // ── Overview interactions ──────────────────────────────────────
    await expect(page.getByTestId('overview-meters')).toBeVisible()
    await expect(page.getByTestId('card-speed')).toBeVisible()
    await expect(page.getByTestId('card-gear')).toBeVisible()
    await expect(page.getByTestId('cmd-feedback')).toBeVisible()
    // Gear is enum — status pill, no meter-bar fill
    await expect(page.getByTestId('status-gear')).toBeVisible()
    await expect(page.getByTestId('card-gear').locator('.meter-bar')).toHaveCount(0)

    // ── Network ────────────────────────────────────────────────────
    await go(page, 'network')
    await expect(page.getByTestId('topology-map')).toBeVisible()
    await expect(page.getByTestId('bus-health')).toBeVisible()

    // ── Live (baseline empty-or-any) ───────────────────────────────
    await go(page, 'live')
    await expect(page.getByTestId('live-can-table')).toBeVisible()
    await page.getByTestId('live-mode-chrono').click()
    await expect(page.getByTestId('live-chrono-table')).toBeVisible()
    await page.getByTestId('live-mode-latest').click()
    await page.getByTestId('filter-bus-high').click()
    await page.getByTestId('filter-bus-low').click()
    await page.getByTestId('filter-bus-both').click()
    await page.getByTestId('live-filter').fill('HOST')
    await page.getByTestId('live-filter').fill('')

    // ── Settings: Computer session first ───────────────────────────
    await go(page, 'settings')
    await expect(page.getByTestId('transport-toggle')).toBeVisible()
    await expect(page.getByTestId('settings-runtime-panel')).toBeVisible()
    await expect(page.getByTestId('settings-protocol-panel')).toBeVisible()
    await page.getByTestId('btn-start-pure').click()
    await expect(page.getByTestId('settings-log')).toContainText(
      /Session|phase|Computer|Active|running/i,
      { timeout: 15_000 },
    )
    await expect(page.getByTestId('settings-active-mode')).toContainText(/Computer|Virtual/i)

    // ── Control: enable TX + inject → Live CAN ─────────────────────
    await go(page, 'control')
    await page.getByTestId('control-method-high').click()
    await page.getByTestId('btn-enable-tx').click()
    await expect(page.getByTestId('control-log')).toContainText(/Bench TX|enabled/i, {
      timeout: 12_000,
    })
    await page.getByTestId('input-speed').fill('750')
    await page.getByTestId('input-yaw').fill('180')
    await page.getByTestId('input-gear').fill('1')
    await page.getByTestId('check-periodic').uncheck()
    await page.getByTestId('btn-inject-drive').click()
    await expect(page.getByTestId('control-log')).toContainText(
      /HOST_DRIVE|submitted|oneshot|High-bus|inject/i,
      { timeout: 12_000 },
    )

    await go(page, 'live')
    await page.getByTestId('live-filter').fill('HOST_DRIVE')
    await expect(page.getByTestId('live-can-table')).toContainText('HOST_DRIVE_CMD', {
      timeout: 15_000,
    })
    // Click a row for detail drawer if present
    const hostRow = page.locator('[data-testid^="row-high-"]').filter({ hasText: 'HOST_DRIVE' }).first()
    if (await hostRow.count()) {
      await hostRow.click()
      await expect(page.getByTestId('live-detail')).toBeVisible()
    }

    await go(page, 'overview')
    // Speed card should reflect inject (or at least remain stable without crash)
    await expect(page.getByTestId('card-speed')).toBeVisible()
    await expect(page.getByTestId('metric-speed')).toBeVisible()

    // ── Low direct actuators ───────────────────────────────────────
    await go(page, 'control')
    await page.getByTestId('control-method-low').click()
    await expect(page.getByTestId('direct-actuators')).toBeVisible()
    await page.getByTestId('direct-motor-speed').fill('400')
    await page.getByTestId('btn-direct-motor-start').click()
    await expect(page.getByTestId('control-log')).toContainText(/motor|direct|start|RT_DRIVE|0x/i, {
      timeout: 12_000,
    })
    await page.getByTestId('btn-direct-motor-stop').click()

    await page.getByTestId('control-method-hmi').click()
    await expect(page.getByTestId('hmi-panel')).toBeVisible()
    await page.getByTestId('btn-mode-manual').click()
    await page.getByTestId('btn-power-on').click()

    await page.getByTestId('control-method-high').click()
    await page.getByTestId('btn-stop-all').click()
    await expect(page.getByTestId('control-log')).toContainText(/Stop All|stop/i, {
      timeout: 10_000,
    })

    // ── Drive: arm + keycap W → CAN ────────────────────────────────
    await go(page, 'preview')
    await page.getByTestId('preview-mode-direct').click()
    await page.getByTestId('preview-gear-D').click()
    await page.getByTestId('preview-canvas-wrap').click()
    await page.getByTestId('btn-drive-arm').click()
    await expect(page.getByTestId('btn-drive-disarm')).toBeVisible({ timeout: 15_000 })
    await page.getByTestId('keycap-W').dispatchEvent('pointerdown')
    await page.waitForTimeout(400)
    await page.getByTestId('keycap-W').dispatchEvent('pointerup')
    await expect(page.getByTestId('drive-shaped')).toContainText(/mm\/s|0|waiting/i, {
      timeout: 8_000,
    })

    await go(page, 'live')
    await page.getByTestId('live-filter').fill('HOST_DRIVE')
    await expect(page.getByTestId('live-can-table')).toContainText('HOST_DRIVE_CMD', {
      timeout: 15_000,
    })

    await go(page, 'preview')
    await page.getByTestId('btn-drive-disarm').click()
    await expect(page.getByTestId('btn-drive-arm')).toBeVisible({ timeout: 10_000 })

    // ── Bench ──────────────────────────────────────────────────────
    await go(page, 'bench')
    await expect(page.getByTestId('workspace-bench')).toContainText(/Bench|profile|TX|virtual/i)

    // ── Dictionary ─────────────────────────────────────────────────
    await go(page, 'dictionary')
    await expect(page.getByTestId('dict-grid')).toBeVisible()
    await page.getByTestId('dict-bus-high').click()
    await page.getByTestId('dict-filter').fill('HOST_DRIVE')
    await expect(page.locator('[data-testid="frame-row"]').first()).toBeVisible({
      timeout: 12_000,
    })
    const bit = page.locator('button.bit-cell.filled').first()
    if (await bit.count()) {
      await bit.hover()
      await expect(page.getByTestId('dict-bit-inspector').first()).toBeVisible()
    }
    const toggle = page.getByTestId('dict-sig-toggle-speed_mmps').first()
    if (await toggle.count()) {
      await toggle.click()
      const exp = page.getByTestId('dict-sig-expand-speed_mmps').first()
      await expect(exp).toBeVisible()
      await expect(exp.getByTestId('dict-expand-what')).toBeVisible()
      await expect(exp.getByTestId('dict-expand-why')).toBeVisible()
      await expect(exp.getByTestId('dict-expand-examples')).toBeVisible()
      await expect(exp).not.toContainText(/Multi-bit field|Bits pack contiguously/i)
    }
    await page.getByTestId('dict-refresh').click()

    // ── Diagnostics recording ──────────────────────────────────────
    await go(page, 'diagnostics')
    await page.getByTestId('btn-rec-start').click()
    await expect(page.getByTestId('recording-log')).toContainText(/Started|rec_/i, {
      timeout: 12_000,
    })
    await page.waitForTimeout(300)
    await page.getByTestId('btn-rec-stop').click()
    await expect(page.getByTestId('recording-log')).toContainText(/Stopped|quality|frames/i, {
      timeout: 12_000,
    })

    // ── Logs ───────────────────────────────────────────────────────
    await go(page, 'logs')
    await page.getByTestId('logs-category').selectOption('control')
    await page.getByTestId('logs-refresh').click()
    await expect(page.getByTestId('logs-table')).toBeVisible()
    await page.getByTestId('logs-category').selectOption('all')

    // ── Header ESTOP (injects dual-bus safety) ─────────────────────
    await page.getByTestId('btn-header-estop').click()
    // Should not crash shell
    await expect(page.getByTestId('app')).toBeVisible()
    await go(page, 'live')
    await page.getByTestId('live-filter').fill('ESTOP')
    // ESTOP may show as SAFETY_ESTOP name
    await expect(page.getByTestId('live-can-table')).toContainText(/ESTOP|SAFETY/i, {
      timeout: 15_000,
    })

    // ── Sidebar open drive ─────────────────────────────────────────
    await page.getByTestId('sidebar-open-drive').click()
    await expect(page.getByTestId('workspace-preview')).toBeVisible()

    // ── Visit remaining tabs once more (nav active) ────────────────
    for (const id of NAV) {
      await go(page, id)
      await expect(page.getByTestId(`nav-${id}`)).toHaveClass(/active/)
    }

    bag.detach()
    if (bag.errors.length) {
      throw new Error(`Runtime issues:\n${bag.errors.join('\n')}`)
    }
  })
})
