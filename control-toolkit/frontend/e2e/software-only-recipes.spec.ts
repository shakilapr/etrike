/**
 * Testing-guide recipes A–E via UI on live stack (5173 → 8001).
 * Screenshots under test-results/software-only-ui/
 */
import { test, expect, type Page } from '@playwright/test'
import fs from 'node:fs'
import path from 'node:path'
import { resetComputerSession } from './session-reset'

const OUT = path.join('test-results', 'software-only-ui')

type Issue = { severity: 'ok' | 'warn' | 'error'; tab: string; message: string }
const issues: Issue[] = []

function note(severity: Issue['severity'], tab: string, message: string) {
  issues.push({ severity, tab, message })
  console.log(`[${severity.toUpperCase()}] ${tab}: ${message}`)
}

async function go(page: Page, id: string) {
  const nav = id === 'preview' ? 'nav-preview' : `nav-${id}`
  const ws = id === 'preview' ? 'workspace-preview' : `workspace-${id}`
  await page.getByTestId(nav).click()
  await expect(page.getByTestId(ws)).toBeVisible({ timeout: 12_000 })
  await page.waitForTimeout(400)
}

async function shot(page: Page, name: string) {
  await page.screenshot({ path: path.join(OUT, name), fullPage: true })
}

async function ensureBenchTx(page: Page) {
  await go(page, 'control')
  const bench = (await page.getByTestId('control-bench-tx').innerText().catch(() => '')).trim()
  if (/^on\b|\benabled\b/i.test(bench)) {
    note('ok', 'control', `Bench TX already ON: ${bench}`)
    return
  }
  if (await page.getByTestId('btn-enable-tx').isVisible().catch(() => false)) {
    await page.getByTestId('btn-enable-tx').click()
    await page.waitForTimeout(700)
  }
  const after = (await page.getByTestId('control-bench-tx').innerText().catch(() => '')).trim()
  if (/^on\b|\benabled\b/i.test(after)) note('ok', 'control', `Bench TX enabled: ${after}`)
  else note('error', 'control', `Bench TX not ON after click: ${after}`)
}

test.describe('Software-only recipes UI', () => {
  test.setTimeout(240_000)

  test.beforeEach(async ({ request }) => {
    await resetComputerSession(request)
  })

  test.afterEach(async ({ request }) => {
    await resetComputerSession(request)
  })

  test('recipes A–E with screenshots', async ({ page }) => {
    fs.mkdirSync(OUT, { recursive: true })
    issues.length = 0
    page.on('pageerror', (e) => note('error', 'runtime', `pageerror: ${e.message}`))

    await page.goto('/')
    await expect(page.getByTestId('app')).toBeVisible({ timeout: 25_000 })
    await page.waitForTimeout(2500)

    const health = await page.getByTestId('chip-health-overall').innerText()
    const stream = await page.getByTestId('chip-stream').innerText()
    note(
      /offline|fault|lost/i.test(health + stream) ? 'error' : 'ok',
      'shell',
      `health=${health.replace(/\s+/g, ' ')} stream=${stream.replace(/\s+/g, ' ')}`,
    )
    await shot(page, '00-shell.png')

    await ensureBenchTx(page)
    await shot(page, '01-control-bench-on.png')

    // ── Recipe A: High host inject periodic ─────────────────────────
    await page.getByTestId('control-method-high').click()
    await expect(page.getByTestId('high-analysis-inject')).toBeVisible()
    await page.getByTestId('input-speed').fill('1500')
    await page.getByTestId('input-yaw').fill('0')
    await page.getByTestId('input-gear').selectOption('1')
    // enable periodic if checkbox exists
    const periodic = page.getByTestId('check-periodic')
    if (await periodic.isVisible().catch(() => false)) {
      if (!(await periodic.isChecked())) await periodic.check()
    }
    const period = page.getByTestId('input-period')
    if (await period.isVisible().catch(() => false)) {
      await period.fill('10')
    }
    await page.getByTestId('btn-inject-drive').click()
    await page.waitForTimeout(1500)
    const clogA = await page.getByTestId('control-log').innerText()
    note(
      /HOST|inject|periodic|submitted|job|ok/i.test(clogA) ? 'ok' : 'error',
      'recipe-A',
      `control-log: ${clogA.slice(0, 160).replace(/\s+/g, ' ')}`,
    )
    await shot(page, '02-recipe-A-high-inject.png')

    // Live CAN — should show 0x300, not low unit ids necessarily
    await go(page, 'live')
    await page.getByTestId('filter-bus-high').click()
    await page.waitForTimeout(800)
    await shot(page, '03-recipe-A-live-high.png')
    const liveHigh = await page.getByTestId('workspace-live').innerText()
    const has300 =
      /0x300|HOST_DRIVE|768/i.test(liveHigh) ||
      (await page.locator('[data-testid^="row-high-"]').count()) > 0
    note(has300 ? 'ok' : 'warn', 'recipe-A', `Live high has host rows/text: ${has300}`)

    // Stop via control stop-all then re-enable bench (P4)
    await go(page, 'control')
    if (await page.getByTestId('btn-stop-all').isVisible()) {
      await page.getByTestId('btn-stop-all').click()
      await page.waitForTimeout(600)
    }
    const benchAfterStop = (await page.getByTestId('control-bench-tx').innerText()).trim()
    if (/off|disabled/i.test(benchAfterStop)) {
      note(
        'warn',
        'plan-P4',
        `Stop-all disabled Bench TX (${benchAfterStop}) — must re-enable before next recipe`,
      )
      if (await page.getByTestId('btn-enable-tx').isVisible().catch(() => false)) {
        await page.getByTestId('btn-enable-tx').click()
        await page.waitForTimeout(500)
      }
    } else {
      note('ok', 'plan-P4', `Bench TX after stop-all: ${benchAfterStop}`)
    }
    await shot(page, '04-after-stop-all.png')

    // ── Recipe B: Low motor ─────────────────────────────────────────
    await page.getByTestId('control-method-low').click()
    await expect(page.getByTestId('direct-actuators')).toBeVisible()
    await expect(page.getByTestId('direct-safety-banner')).toBeVisible()
    // set motor speed if input exists
    const motorSpeed = page.getByTestId('direct-motor-speed')
    if (await motorSpeed.isVisible().catch(() => false)) {
      const tag = await motorSpeed.evaluate((el) => el.tagName)
      if (tag === 'INPUT') await motorSpeed.fill('800')
    }
    await page.getByTestId('btn-direct-motor-start').click()
    await page.waitForTimeout(1200)
    const motorTx = await page.getByTestId('direct-motor-tx').innerText()
    note(
      /0x204|speed=\d|live|tx/i.test(motorTx) ? 'ok' : 'error',
      'recipe-B',
      `motor-tx: ${motorTx.slice(0, 120)}`,
    )
    await shot(page, '05-recipe-B-motor.png')

    await go(page, 'live')
    await page.getByTestId('filter-bus-low').click()
    await page.waitForTimeout(600)
    await shot(page, '06-recipe-B-live-low.png')
    const liveLowB = await page.getByTestId('workspace-live').innerText()
    note(
      /0x204|RT_DRIVE|516/i.test(liveLowB) ||
        (await page.locator('[data-testid*="204"]').count()) > 0 ||
        (await page.locator('[data-testid^="row-low-"]').count()) > 0
        ? 'ok'
        : 'warn',
      'recipe-B',
      'Live low after motor start',
    )

    await go(page, 'control')
    await page.getByTestId('control-method-low').click()

    // ── Recipe C: Steering ──────────────────────────────────────────
    const steerIn = page.getByTestId('direct-steer-angle')
    if (await steerIn.isVisible().catch(() => false)) {
      const tag = await steerIn.evaluate((el) => el.tagName)
      if (tag === 'INPUT') await steerIn.fill('100')
    }
    await page.getByTestId('btn-direct-steer-start').click()
    await page.waitForTimeout(1200)
    const steerTx = await page.getByTestId('direct-steer-tx').innerText()
    note(
      /0x169|angle|live|tx|en=1/i.test(steerTx) ? 'ok' : 'error',
      'recipe-C',
      `steer-tx: ${steerTx.slice(0, 120)}`,
    )
    await shot(page, '07-recipe-C-steer.png')
    await page.getByTestId('btn-direct-steer-stop').click()
    await page.waitForTimeout(300)

    // ── Recipe D: Brake ─────────────────────────────────────────────
    const brakeIn = page.getByTestId('direct-brake-pressure')
    if (await brakeIn.isVisible().catch(() => false)) {
      const tag = await brakeIn.evaluate((el) => el.tagName)
      if (tag === 'INPUT') await brakeIn.fill('40')
    }
    await page.getByTestId('btn-direct-brake-start').click()
    await page.waitForTimeout(1200)
    const brakeTx = await page.getByTestId('direct-brake-tx').innerText()
    note(
      /0x7B9|pressure|live|tx|en=1/i.test(brakeTx) ? 'ok' : 'error',
      'recipe-D',
      `brake-tx: ${brakeTx.slice(0, 120)}`,
    )
    await shot(page, '08-recipe-D-brake.png')
    await page.getByTestId('btn-direct-brake-stop').click()
    await page.waitForTimeout(300)

    // ── Recipe E: all three ─────────────────────────────────────────
    await page.getByTestId('btn-direct-motor-start').click()
    await page.getByTestId('btn-direct-steer-start').click()
    await page.getByTestId('btn-direct-brake-start').click()
    await page.waitForTimeout(1500)
    const m = await page.getByTestId('direct-motor-tx').innerText()
    const s = await page.getByTestId('direct-steer-tx').innerText()
    const b = await page.getByTestId('direct-brake-tx').innerText()
    const eOk =
      /0x204|speed=/i.test(m) && /0x169|angle/i.test(s) && /0x7B9|pressure/i.test(b)
    note(eOk ? 'ok' : 'warn', 'recipe-E', `motor=${m.slice(0, 60)} | steer=${s.slice(0, 60)} | brake=${b.slice(0, 60)}`)
    await shot(page, '09-recipe-E-all-three.png')

    await go(page, 'live')
    await page.getByTestId('filter-bus-low').click()
    await page.getByTestId('live-mode-chrono').click()
    await page.waitForTimeout(800)
    await shot(page, '10-recipe-E-live-chrono.png')

    // Overview while motion active
    await go(page, 'overview')
    await shot(page, '11-overview-during-motion.png')
    const sideSpeed = await page.getByTestId('sidebar-speed').innerText().catch(() => '—')
    note('ok', 'overview', `sidebar-speed during low direct: ${sideSpeed.replace(/\s+/g, ' ')}`)

    // Cleanup
    await go(page, 'control')
    await page.getByTestId('btn-stop-all').click()
    await page.waitForTimeout(500)
    await shot(page, '12-final-stop.png')

    // Drive arm smoke (High path)
    await ensureBenchTx(page)
    await go(page, 'preview')
    if (await page.getByTestId('btn-drive-arm').isVisible().catch(() => false)) {
      await page.getByTestId('btn-drive-arm').click()
      await page.waitForTimeout(800)
      const chip = await page.getByTestId('drive-arm-chip').innerText().catch(() => '')
      note(/armed/i.test(chip) ? 'ok' : 'warn', 'drive', `arm chip: ${chip.replace(/\s+/g, ' ')}`)
      await shot(page, '13-drive-armed.png')
      if (await page.getByTestId('btn-drive-disarm').isVisible().catch(() => false)) {
        await page.getByTestId('btn-drive-disarm').click()
      }
    } else {
      note('error', 'drive', 'btn-drive-arm missing')
    }

    // Network topology offline expected
    await go(page, 'network')
    await shot(page, '14-network-topology.png')
    const net = await page.getByTestId('workspace-network').innerText()
    if (/offline/i.test(net)) {
      note('ok', 'network', 'Topology shows offline nodes (expected without ECU peers)')
    } else {
      note('warn', 'network', 'Expected offline topology labels not obvious')
    }

    const summary = {
      ok: issues.filter((i) => i.severity === 'ok').length,
      warn: issues.filter((i) => i.severity === 'warn').length,
      error: issues.filter((i) => i.severity === 'error').length,
    }
    const report = {
      generatedAt: new Date().toISOString(),
      summary,
      issues,
      screenshots: fs.readdirSync(OUT).filter((f) => f.endsWith('.png')),
    }
    fs.writeFileSync(path.join(OUT, 'report.json'), JSON.stringify(report, null, 2))
    const md = [
      '# Software-only UI recipe report',
      '',
      `Generated: ${report.generatedAt}`,
      '',
      `OK ${summary.ok} · WARN ${summary.warn} · ERROR ${summary.error}`,
      '',
      '| Severity | Tab | Message |',
      '|----------|-----|---------|',
      ...issues.map(
        (i) => `| ${i.severity} | ${i.tab} | ${i.message.replace(/\|/g, '\\|')} |`,
      ),
      '',
      '## Screenshots',
      '',
      ...report.screenshots.map((s) => `- \`${s}\``),
    ].join('\n')
    fs.writeFileSync(path.join(OUT, 'REPORT.md'), md)
    console.log('SUMMARY', summary)
    expect(summary.error, JSON.stringify(issues.filter((i) => i.severity === 'error'), null, 2)).toBe(0)
  })
})
