/**
 * Walk every tab, exercise primary controls, collect UI issues.
 */
import { test, expect, type Page } from '@playwright/test'
import fs from 'node:fs'
import path from 'node:path'

const OUT = path.join('test-results', 'ui-issues')

type Issue = { tab: string; severity: 'error' | 'warn'; message: string }

async function go(page: Page, id: string) {
  const nav = id === 'preview' ? 'nav-preview' : `nav-${id}`
  const ws = id === 'preview' ? 'workspace-preview' : `workspace-${id}`
  await page.getByTestId(nav).click()
  await expect(page.getByTestId(ws)).toBeVisible({ timeout: 12_000 })
}

test.describe('UI issues audit — every tab', () => {
  test.setTimeout(240_000)

  test('exercise all workspaces and flag UI problems', async ({ page }) => {
    fs.mkdirSync(OUT, { recursive: true })
    const issues: Issue[] = []
    const pageErrors: string[] = []
    page.on('pageerror', (e) => pageErrors.push(e.message))

    await page.goto('/')
    await expect(page.getByTestId('app')).toBeVisible({ timeout: 20_000 })
    await page.waitForTimeout(2500)

    // Shell
    const health = await page.getByTestId('chip-health-overall').innerText()
    const stream = await page.getByTestId('chip-stream').innerText()
    if (/offline/i.test(health) && /lost/i.test(stream)) {
      issues.push({
        tab: 'shell',
        severity: 'error',
        message: `Shell Offline/Lost at start: health=${health} stream=${stream}`,
      })
    }
    await page.screenshot({ path: path.join(OUT, '00-shell.png') })

    // Overview
    await go(page, 'overview')
    await expect(page.getByTestId('safety-strip')).toBeVisible()
    await expect(page.getByTestId('overview-meters')).toBeVisible()
    // Binary chips should not be meter bars for ESTOP/bench
    const estopCls = await page.getByTestId('meter-estop').getAttribute('class')
    if (estopCls?.includes('meter-bar')) {
      issues.push({ tab: 'overview', severity: 'error', message: 'ESTOP still uses meter-bar' })
    }
    if (!estopCls?.includes('status-pill')) {
      issues.push({ tab: 'overview', severity: 'warn', message: 'ESTOP missing status-pill class' })
    }
    await page.screenshot({ path: path.join(OUT, '01-overview.png') })

    // Network
    await go(page, 'network')
    await expect(page.getByTestId('topology-map')).toBeVisible()
    await page.screenshot({ path: path.join(OUT, '02-network.png') })

    // Live
    await go(page, 'live')
    await page.getByTestId('live-mode-chrono').click()
    await expect(page.getByTestId('live-chrono-table')).toBeVisible()
    await page.getByTestId('live-mode-latest').click()
    await page.getByTestId('filter-bus-high').click()
    await page.getByTestId('filter-bus-low').click()
    await page.getByTestId('filter-bus-both').click()
    await page.screenshot({ path: path.join(OUT, '03-live.png') })

    // Control — full exercise
    await go(page, 'control')
    await expect(page.getByTestId('control-session-panel')).toBeVisible()
    await page.getByTestId('btn-enable-tx').click()
    await expect(page.getByTestId('control-log')).toContainText(/Bench TX|enabled|gate|ON/i, {
      timeout: 12_000,
    })
    await expect(page.getByTestId('control-bench-tx')).toContainText(/ON|enabled/i)
    // When on, should show disable, not only "Enable"
    await expect(page.getByTestId('btn-disable-tx')).toBeVisible()
    await page.getByTestId('control-method-high').click()
    await page.getByTestId('check-periodic').uncheck()
    await page.getByTestId('input-speed').fill('550')
    await page.getByTestId('input-yaw').fill('120')
    await page.getByTestId('input-gear').selectOption('1')
    await page.getByTestId('btn-inject-drive').click()
    await expect(page.getByTestId('control-log')).toContainText(/HOST_DRIVE|inject|oneshot|submitted/i, {
      timeout: 12_000,
    })
    await page.getByTestId('control-method-low').click()
    await expect(page.getByTestId('direct-safety-banner')).toBeVisible()
    await page.getByTestId('btn-direct-motor-start').click()
    await page.waitForTimeout(800)
    await expect(page.getByTestId('control-log')).toContainText(/motor|low|direct|channel/i, {
      timeout: 10_000,
    })
    const motorTx = await page.getByTestId('direct-motor-tx').innerText()
    if (/no frame yet|speed=—/i.test(motorTx) && !/speed=\d/i.test(motorTx)) {
      issues.push({
        tab: 'control-low',
        severity: 'warn',
        message: `Motor TX line empty after start: ${motorTx}`,
      })
    }
    await page.getByTestId('btn-direct-steer-start').click()
    await page.waitForTimeout(600)
    await page.getByTestId('btn-direct-brake-start').click()
    await page.waitForTimeout(600)
    const steerTx = await page.getByTestId('direct-steer-tx').innerText()
    if (!/en=1\/1|en=1/.test(steerTx) && !/alignment|control/i.test(steerTx)) {
      // en=1/1 is ideal
      if (/en=—/.test(steerTx)) {
        issues.push({
          tab: 'control-low',
          severity: 'warn',
          message: `Steer TX missing enable bits: ${steerTx}`,
        })
      }
    }
    await page.getByTestId('control-method-hmi').click()
    await page.getByTestId('btn-mode-manual').click()
    await page.waitForTimeout(500)
    await page.screenshot({ path: path.join(OUT, '04-control.png') })

    // Drive
    await go(page, 'preview')
    await page.getByTestId('btn-drive-arm').click()
    await expect(page.getByTestId('btn-drive-disarm')).toBeVisible({ timeout: 15_000 })
    await page.getByTestId('keycap-W').dispatchEvent('pointerdown')
    await page.waitForTimeout(400)
    await page.getByTestId('keycap-W').dispatchEvent('pointerup')
    await page.getByTestId('btn-drive-disarm').click()
    await page.screenshot({ path: path.join(OUT, '05-drive.png') })

    // Bench
    await go(page, 'bench')
    const benchText = await page.getByTestId('workspace-bench').innerText()
    if (!/Bench|TX|profile|virtual|physical/i.test(benchText)) {
      issues.push({ tab: 'bench', severity: 'error', message: 'Bench workspace sparse content' })
    }
    await page.screenshot({ path: path.join(OUT, '06-bench.png') })

    // Dictionary
    await go(page, 'dictionary')
    await page.getByTestId('dict-filter').fill('HOST_DRIVE')
    await expect(page.locator('[data-testid="frame-row"]').first()).toBeVisible({
      timeout: 15_000,
    })
    const dictErr = await page.locator('.dict-err').count()
    if (dictErr) {
      issues.push({
        tab: 'dictionary',
        severity: 'error',
        message: `Dictionary error: ${await page.locator('.dict-err').innerText()}`,
      })
    }
    const toggle = page.getByTestId('dict-sig-toggle-speed_mmps').first()
    if (await toggle.count()) {
      await toggle.click()
      await expect(page.getByTestId('dict-sig-expand-speed_mmps').first()).toBeVisible()
      await expect(page.getByTestId('dict-expand-what').first()).toBeVisible()
    }
    await page.screenshot({ path: path.join(OUT, '07-dictionary.png') })

    // Diagnostics
    await go(page, 'diagnostics')
    await expect(page.getByTestId('recording-panel')).toBeVisible()
    await page.getByTestId('btn-rec-start').click()
    await expect(page.getByTestId('recording-log')).toContainText(/Started|rec_/i, {
      timeout: 12_000,
    })
    await page.waitForTimeout(400)
    await page.getByTestId('btn-rec-stop').click()
    await expect(page.getByTestId('recording-log')).toContainText(/Stopped|quality|frames/i, {
      timeout: 12_000,
    })
    await page.screenshot({ path: path.join(OUT, '08-diagnostics.png') })

    // Logs
    await go(page, 'logs')
    await page.getByTestId('logs-refresh').click()
    await page.waitForTimeout(600)
    const logsErr = await page.locator('[data-testid=workspace-logs] .danger-text').count()
    if (logsErr) {
      issues.push({
        tab: 'logs',
        severity: 'error',
        message: `Logs danger text: ${await page.locator('[data-testid=workspace-logs] .danger-text').first().innerText()}`,
      })
    }
    const logsBody = await page.getByTestId('logs-table').innerText()
    if (/No log entries/i.test(logsBody) && !/INFO|session|recording|backend/i.test(logsBody)) {
      issues.push({
        tab: 'logs',
        severity: 'warn',
        message: 'Logs table empty after control activity',
      })
    }
    await page.screenshot({ path: path.join(OUT, '09-logs.png') })

    // Settings
    await go(page, 'settings')
    await expect(page.getByTestId('transport-toggle')).toBeVisible()
    await expect(page.getByTestId('settings-runtime-panel')).toBeVisible()
    await page.getByTestId('btn-start-pure').click()
    await expect(page.getByTestId('settings-log')).toContainText(/Session|Computer|Active|running/i, {
      timeout: 12_000,
    })
    await page.screenshot({ path: path.join(OUT, '10-settings.png') })

    // Live again after activity — should show HOST or low frames
    await go(page, 'live')
    await page.getByTestId('filter-bus-both').click()
    await page.getByTestId('live-filter').fill('')
    await page.waitForTimeout(800)
    const liveText = await page.getByTestId('live-can-table').innerText()
    if (/No frames yet/i.test(liveText)) {
      issues.push({
        tab: 'live',
        severity: 'warn',
        message: 'Live CAN still empty after control inject/direct',
      })
    }

    // Overlapping primary buttons check on control
    await go(page, 'control')
    const enableBox = await page.getByTestId('btn-enable-tx').boundingBox()
    const stopBox = await page.getByTestId('btn-stop-all').boundingBox()
    if (enableBox && stopBox) {
      const overlap =
        enableBox.x < stopBox.x + stopBox.width &&
        enableBox.x + enableBox.width > stopBox.x &&
        enableBox.y < stopBox.y + stopBox.height &&
        enableBox.y + enableBox.height > stopBox.y
      if (overlap) {
        issues.push({
          tab: 'control',
          severity: 'error',
          message: 'btn-enable-tx overlaps btn-stop-all',
        })
      }
    }

    for (const pe of pageErrors) {
      issues.push({ tab: 'global', severity: 'error', message: `pageerror: ${pe}` })
    }

    const report = {
      health,
      stream,
      issues,
      errorCount: issues.filter((i) => i.severity === 'error').length,
      warnCount: issues.filter((i) => i.severity === 'warn').length,
    }
    fs.writeFileSync(path.join(OUT, 'report.json'), JSON.stringify(report, null, 2))
    console.log('=== UI ISSUES REPORT ===')
    console.log(JSON.stringify(report, null, 2))

    expect(report.errorCount, JSON.stringify(issues, null, 2)).toBe(0)
  })
})
