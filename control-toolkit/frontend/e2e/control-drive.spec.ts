/**
 * Control + Drive: exercise every motion path, assert backend reaction via UI log + Live CAN.
 */
import { test, expect, type Page } from '@playwright/test'
import fs from 'node:fs'
import path from 'node:path'
import { resetComputerSession } from './session-reset'

const OUT = path.join('test-results', 'control-drive')

type Issue = { area: string; severity: 'error' | 'warn'; message: string }

async function go(page: Page, id: string) {
  const nav = id === 'preview' ? 'nav-preview' : `nav-${id}`
  const ws = id === 'preview' ? 'workspace-preview' : `workspace-${id}`
  await page.getByTestId(nav).click()
  await expect(page.getByTestId(ws)).toBeVisible({ timeout: 15_000 })
}

test.describe('Control + Drive paths', () => {
  test.setTimeout(240_000)

  test('high inject, keyboard, low actuators, HMI, drive arm, live CAN', async ({ page, request }) => {
    fs.mkdirSync(OUT, { recursive: true })
    const issues: Issue[] = []
    const pageErrors: string[] = []
    page.on('pageerror', (e) => pageErrors.push(e.message))

    await resetComputerSession(request)
    await page.goto('/')
    await expect(page.getByTestId('app')).toBeVisible({ timeout: 20_000 })
    await expect(page.getByTestId('chip-stream')).not.toHaveText(/CONNECTING/i, {
      timeout: 25_000,
    })
    await page.waitForTimeout(1200)

    // ── Control session gate ──────────────────────────────────────
    await go(page, 'control')
    await expect(page.getByTestId('control-session-panel')).toBeVisible()
    await page.getByTestId('btn-enable-tx').click()
    await expect(page.getByTestId('control-bench-tx')).toContainText(/ON — bus TX allowed|enabled/i, {
      timeout: 12_000,
    })
    await expect(page.getByTestId('btn-disable-tx')).toBeVisible()
    await page.screenshot({ path: path.join(OUT, '01-control-session.png') })

    // ── High · oneshot inject ─────────────────────────────────────
    await page.getByTestId('control-method-high').click()
    await page.getByTestId('check-periodic').uncheck()
    await page.getByTestId('input-speed').fill('550')
    await page.getByTestId('input-yaw').fill('120')
    await page.getByTestId('input-gear').selectOption('1')
    await page.getByTestId('btn-inject-drive').click()
    await expect(page.getByTestId('control-log')).toContainText(
      /HOST_DRIVE|inject|oneshot|submitted|High-bus/i,
      { timeout: 12_000 },
    )
    const injectLog = await page.getByTestId('control-log').innerText()
    if (/409|rejected|ownership/i.test(injectLog) && !/oneshot|ok|HOST_DRIVE/i.test(injectLog)) {
      issues.push({
        area: 'control-high',
        severity: 'error',
        message: `Inject failed: ${injectLog.slice(0, 200)}`,
      })
    }
    await expect(page.getByTestId('control-active-method')).toContainText(/high|none|low/i)

    // Live CAN should show HOST after inject
    await go(page, 'live')
    await page.getByTestId('filter-bus-high').click()
    await page.getByTestId('live-filter').fill('HOST')
    await page.waitForTimeout(600)
    const liveHigh = await page.getByTestId('live-can-table').innerText()
    if (!/HOST_DRIVE|0x300|300/i.test(liveHigh)) {
      issues.push({
        area: 'live-high',
        severity: 'warn',
        message: `Live high empty after inject: ${liveHigh.slice(0, 120)}`,
      })
    }
    await page.screenshot({ path: path.join(OUT, '02-live-high.png') })

    // ── Control keyboard teleop ───────────────────────────────────
    await go(page, 'control')
    await page.getByTestId('control-method-high').click()
    await page.getByTestId('btn-kb-enable').click()
    await expect(page.getByTestId('kb-active-banner')).toBeVisible({ timeout: 10_000 })
    await page.keyboard.down('w')
    await page.waitForTimeout(500)
    await page.keyboard.up('w')
    await page.waitForTimeout(400)
    await expect(page.getByTestId('kb-shaped')).toBeVisible({ timeout: 8_000 })
    const kbShaped = await page.getByTestId('kb-shaped').innerText()
    // After releasing W, shaped may return to 0 quickly (stale 500ms / zero intent).
    // While held we expect non-zero; after release check that panel still renders.
    if (!/Shaped speed|mm\/s|Gear/i.test(kbShaped)) {
      issues.push({
        area: 'control-keyboard',
        severity: 'error',
        message: `Keyboard shaped panel incomplete: ${kbShaped}`,
      })
    }
    // Re-hold W and sample while down
    await page.keyboard.down('w')
    await page.waitForTimeout(350)
    const kbWhile = await page.getByTestId('kb-shaped').innerText()
    await expect.poll(async () => {
      const response = await page.request.get('/api/v1/state')
      const messages = ((await response.json()) as { messages?: Array<{
        name?: string
        signals?: Record<string, { engineering_value?: number }>
      }> }).messages ?? []
      const rtDrive = messages.find((message) => message.name === 'RT_DRIVE_CMD')
      return Number(rtDrive?.signals?.motor_speed_mmps?.engineering_value ?? 0)
    }, {
      message: 'keyboard W must reach native RT SIL and return RT_DRIVE_CMD',
      timeout: 8_000,
    }).toBeGreaterThan(0)
    await page.keyboard.up('w')
    if (!/\b[1-9]\d{2,}\b/.test(kbWhile) && !/1500|3000|mm\/s/.test(kbWhile)) {
      // shaped_speed should be ~3000 at full throttle
      issues.push({
        area: 'control-keyboard',
        severity: 'error',
        message: `Keyboard W did not shape speed: ${kbWhile}`,
      })
    }
    await page.getByTestId('btn-kb-enable').click() // stop keyboard

    // ── Low · motor / steer / brake ───────────────────────────────
    await page.getByTestId('control-method-low').click()
    await expect(page.getByTestId('direct-safety-banner')).toBeVisible()
    await page.getByTestId('direct-motor-speed').fill('700')
    await page.getByTestId('btn-direct-motor-start').click()
    await expect(page.getByTestId('control-log')).toContainText(/motor|Low TX|low_direct/i, {
      timeout: 12_000,
    })
    await page.waitForTimeout(500)
    const motorTx = await page.getByTestId('direct-motor-tx').innerText()
    if (/no frame yet/i.test(motorTx) && !/700|speed=\d/i.test(motorTx)) {
      issues.push({
        area: 'control-low-motor',
        severity: 'error',
        message: `Motor TX empty: ${motorTx}`,
      })
    }
    await page.getByTestId('btn-direct-steer-start').click()
    await page.waitForTimeout(500)
    const steerTx = await page.getByTestId('direct-steer-tx').innerText()
    if (/en=—|no frame/i.test(steerTx) && !/en=1/i.test(steerTx)) {
      issues.push({
        area: 'control-low-steer',
        severity: 'error',
        message: `Steer TX incomplete: ${steerTx}`,
      })
    }
    await page.getByTestId('btn-direct-brake-start').click()
    await page.waitForTimeout(500)
    const brakeTx = await page.getByTestId('direct-brake-tx').innerText()
    if (/no frame|en=—/i.test(brakeTx) && !/en=1|pressure/i.test(brakeTx)) {
      issues.push({
        area: 'control-low-brake',
        severity: 'warn',
        message: `Brake TX: ${brakeTx}`,
      })
    }
    await expect(page.getByTestId('control-active-method')).toContainText(/low_direct/i, {
      timeout: 8_000,
    })
    await page.screenshot({ path: path.join(OUT, '03-control-low.png') })

    // Live low
    await go(page, 'live')
    await page.getByTestId('filter-bus-low').click()
    await page.getByTestId('live-filter').fill('')
    await page.waitForTimeout(700)
    const liveLow = await page.getByTestId('live-can-table').innerText()
    if (!/RT_DRIVE|0x204|VCU_SES|0x169|VCU_SEB|0x7B9/i.test(liveLow)) {
      issues.push({
        area: 'live-low',
        severity: 'error',
        message: `Live low missing actuator frames: ${liveLow.slice(0, 160)}`,
      })
    }

    // ── High preempts low (inject after low) ──────────────────────
    await go(page, 'control')
    await page.getByTestId('control-method-high').click()
    await page.getByTestId('btn-inject-drive').click()
    await expect(page.getByTestId('control-log')).toContainText(/inject|HOST_DRIVE|High-bus/i, {
      timeout: 12_000,
    })
    // After inject, control release clears direct; method may be none after oneshot
    await page.waitForTimeout(400)

    // ── HMI ───────────────────────────────────────────────────────
    await page.getByTestId('control-method-hmi').click()
    await page.getByTestId('btn-mode-manual').click()
    await expect(page.getByTestId('control-log')).toContainText(/HMI mode|MANUAL/i, {
      timeout: 10_000,
    })
    await page.getByTestId('btn-power-on').click()
    await expect(page.getByTestId('control-log')).toContainText(/HMI power|ON/i, {
      timeout: 10_000,
    })
    await page.screenshot({ path: path.join(OUT, '04-control-hmi.png') })

    // ── Drive arm + keycaps ───────────────────────────────────────
    await go(page, 'preview')
    await page.getByTestId('btn-drive-arm').click()
    await expect(page.getByTestId('btn-drive-disarm')).toBeVisible({ timeout: 15_000 })
    // Safety check: ESTOP button is isolated and meets emergency target sizing
    const estopBtn = page.getByTestId('btn-drive-estop')
    await expect(estopBtn).toBeVisible()
    await expect(estopBtn).toHaveClass(/btn-drive-estop/)
    const estopBox = await estopBtn.boundingBox()
    if (estopBox && estopBox.height < 36) {
      issues.push({
        area: 'drive-safety',
        severity: 'error',
        message: `ESTOP button height ${estopBox.height}px < 36px`,
      })
    }
    // Ergonomic checks: Center vehicle reset button
    const resetBtn = page.getByTestId('btn-reset-view')
    await expect(resetBtn).toBeVisible()
    await resetBtn.click()

    // Bipolar steering gauge
    const steerGauge = page.getByTestId('gauge-steer α')
    await expect(steerGauge).toBeVisible()
    await expect(steerGauge).toHaveClass(/is-bipolar/)
    await expect(steerGauge.locator('.drive-gauge-center')).toBeVisible()

    // Authority limit presets and human-readable units
    await expect(page.getByTestId('drive-limit-presets')).toBeVisible()
    await expect(page.getByText(/km\/h/i)).toBeVisible()
    await expect(page.getByText(/°\/s/i)).toBeVisible()
    // Click Crawl preset
    await page.getByRole('button', { name: /Crawl/i }).click()
    // Click Max preset
    await page.getByRole('button', { name: /Max/i }).click()

    // Adaptive mode gear hint
    await expect(page.getByTestId('preview-gear-auto-hint')).toBeVisible()
    await expect(page.getByTestId('preview-gears')).toHaveClass(/is-auto/)
    // Switch to Direct mode to verify hint disappears
    await page.getByTestId('preview-mode-direct').click()
    await expect(page.getByTestId('preview-gear-auto-hint')).not.toBeVisible()
    await expect(page.getByTestId('preview-gears')).not.toHaveClass(/is-auto/)
    // Switch back to Adaptive mode
    await page.getByTestId('preview-mode-adaptive').click()
    await expect(page.getByTestId('preview-gear-auto-hint')).toBeVisible()

    // Keycaps visible without scroll (layout fix)
    await expect(page.getByTestId('drive-keycaps')).toBeVisible()
    await expect(page.getByTestId('keycap-W')).toBeVisible()
    const wBox = await page.getByTestId('keycap-W').boundingBox()
    if (wBox && wBox.y > 850) {
      issues.push({
        area: 'drive-layout',
        severity: 'warn',
        message: `keycap-W y=${wBox.y} may be below fold`,
      })
    }
    // Hold W via pointer (real press path)
    await page.getByTestId('keycap-W').dispatchEvent('pointerdown')
    await page.waitForTimeout(600)
    await page.getByTestId('keycap-W').dispatchEvent('pointerup')
    await page.waitForTimeout(300)
    // Steer A to test bipolar steering reaction
    await page.getByTestId('keycap-A').dispatchEvent('pointerdown')
    await page.waitForTimeout(400)
    await page.getByTestId('keycap-A').dispatchEvent('pointerup')
    await page.waitForTimeout(200)

    // Gear D
    await page.getByTestId('preview-gear-D').click()
    await page.getByTestId('keycap-W').dispatchEvent('pointerdown')
    await page.waitForTimeout(500)
    await page.getByTestId('keycap-W').dispatchEvent('pointerup')
    const driveLog = await page.getByTestId('drive-log').innerText()
    // shaped display
    const shaped = await page.getByTestId('drive-shaped').innerText()

    // Explicit Reverse + positive pedal/key input must remain bounded reverse.
    await page.getByTestId('preview-gear-R').click()
    await page.getByTestId('keycap-W').dispatchEvent('pointerdown')
    await expect(page.getByTestId('drive-shaped')).toContainText(/-500 mm\/s · R/i, {
      timeout: 8_000,
    })
    await page.getByTestId('keycap-W').dispatchEvent('pointerup')
    await page.screenshot({ path: path.join(OUT, '05-drive-armed.png') })

    // Live after drive
    await go(page, 'live')
    await page.getByTestId('filter-bus-both').click()
    await page.getByTestId('live-filter').fill('HOST')
    await page.waitForTimeout(800)
    const liveDrive = await page.getByTestId('live-can-table').innerText()
    if (!/HOST_DRIVE|0x300/i.test(liveDrive)) {
      issues.push({
        area: 'drive-live',
        severity: 'warn',
        message: `No HOST after Drive arm/W: ${liveDrive.slice(0, 120)} · driveLog=${driveLog.slice(0, 80)} · shaped=${shaped}`,
      })
    }

    // Leaving Drive unmounts and disarms (safety). Re-enter should show Arm, not Disarm.
    await go(page, 'preview')
    await expect(page.getByTestId('btn-drive-arm')).toBeVisible({ timeout: 10_000 })
    // Re-arm to prove sequence is not stuck after Control activity
    await page.getByTestId('btn-drive-arm').click()
    await expect(page.getByTestId('btn-drive-disarm')).toBeVisible({ timeout: 15_000 })
    await page.getByTestId('keycap-W').dispatchEvent('pointerdown')
    await page.waitForTimeout(500)
    await page.getByTestId('keycap-W').dispatchEvent('pointerup')
    await page.waitForTimeout(300)
    const shaped2 = await page.getByTestId('drive-shaped').innerText()
    if (!/\d/.test(shaped2)) {
      issues.push({
        area: 'drive-rearm',
        severity: 'error',
        message: `Drive re-arm after Control still no shaped telemetry: ${shaped2}`,
      })
    }
    await page.getByTestId('btn-drive-disarm').click()
    await expect(page.getByTestId('btn-drive-arm')).toBeVisible({ timeout: 10_000 })

    // Stop all from control
    await go(page, 'control')
    await page.getByTestId('btn-stop-all').click()
    await expect(page.getByTestId('control-log')).toContainText(/Stop|cleared|released/i, {
      timeout: 10_000,
    })

    for (const pe of pageErrors) {
      if (/setPointerCapture/i.test(pe)) {
        issues.push({ area: 'global', severity: 'error', message: `pageerror: ${pe}` })
      } else {
        issues.push({ area: 'global', severity: 'error', message: `pageerror: ${pe}` })
      }
    }

    const report = {
      issues,
      errorCount: issues.filter((i) => i.severity === 'error').length,
      warnCount: issues.filter((i) => i.severity === 'warn').length,
      injectLog: injectLog.slice(0, 300),
      motorTx,
      steerTx,
      brakeTx,
      shaped,
      driveLog: driveLog.slice(0, 200),
    }
    fs.writeFileSync(path.join(OUT, 'report.json'), JSON.stringify(report, null, 2))
    console.log('=== CONTROL/DRIVE REPORT ===')
    console.log(JSON.stringify(report, null, 2))
    expect(report.errorCount, JSON.stringify(issues, null, 2)).toBe(0)
  })
})
