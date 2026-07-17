/**
 * Live click-through of every workspace against the developer stack
 * (127.0.0.1:5173 → proxy → 8001). Reports missing / poorly wired UI.
 *
 * Run:
 *   npx playwright test e2e/live-click-audit.spec.ts --config=playwright.live.config.ts
 */
import { test, expect, type Page, type APIRequestContext } from '@playwright/test'
import fs from 'node:fs'
import path from 'node:path'

const OUT = path.join('test-results', 'live-click-audit')

type Severity = 'ok' | 'warn' | 'error' | 'missing'
type Finding = {
  tab: string
  control: string
  severity: Severity
  detail: string
  http?: number
  apiPath?: string
}

const findings: Finding[] = []
const pageErrors: string[] = []
const consoleErrors: string[] = []
const failedRequests: Array<{ url: string; status: number; method: string }> = []

function note(f: Finding) {
  findings.push(f)
  const mark =
    f.severity === 'ok' ? 'OK' : f.severity === 'warn' ? 'WARN' : f.severity === 'error' ? 'ERR' : 'MISS'
  console.log(`  [${mark}] ${f.tab}/${f.control}: ${f.detail}`)
}

async function go(page: Page, id: string) {
  const nav = id === 'preview' ? 'nav-preview' : `nav-${id}`
  const ws = id === 'preview' ? 'workspace-preview' : `workspace-${id}`
  await page.getByTestId(nav).click()
  await expect(page.getByTestId(ws)).toBeVisible({ timeout: 15_000 })
  await page.waitForTimeout(400)
}

async function visible(page: Page, testId: string): Promise<boolean> {
  return page.getByTestId(testId).isVisible().catch(() => false)
}

async function clickIf(page: Page, testId: string): Promise<boolean> {
  const el = page.getByTestId(testId)
  if (!(await el.isVisible().catch(() => false))) return false
  if (await el.isDisabled().catch(() => true)) return false
  await el.click()
  return true
}

async function textOf(page: Page, testId: string): Promise<string> {
  try {
    return (await page.getByTestId(testId).innerText()).trim()
  } catch {
    return ''
  }
}

async function probeApi(
  request: APIRequestContext,
  method: string,
  apiPath: string,
  body?: unknown,
): Promise<{ status: number; ok: boolean; snippet: string }> {
  const url = `http://127.0.0.1:8001/api/v1${apiPath}`
  try {
    const r = await request.fetch(url, {
      method,
      headers: body ? { 'Content-Type': 'application/json' } : undefined,
      data: body !== undefined ? JSON.stringify(body) : undefined,
      failOnStatusCode: false,
    })
    const t = await r.text()
    return {
      status: r.status(),
      ok: r.ok(),
      snippet: t.slice(0, 180).replace(/\s+/g, ' '),
    }
  } catch (e) {
    return { status: 0, ok: false, snippet: String(e) }
  }
}

test.describe('Live click audit — every workspace', () => {
  test.setTimeout(480_000)

  test('click through all tabs and report wiring', async ({ page, request }) => {
    fs.mkdirSync(OUT, { recursive: true })
    findings.length = 0
    pageErrors.length = 0
    consoleErrors.length = 0
    failedRequests.length = 0

    page.on('pageerror', (e) => pageErrors.push(e.message))
    page.on('console', (msg) => {
      if (msg.type() === 'error') consoleErrors.push(msg.text())
    })
    page.on('response', (res) => {
      const u = res.url()
      if (u.includes('/api/v1/') && res.status() >= 400) {
        failedRequests.push({
          url: u.replace(/^https?:\/\/[^/]+/, ''),
          status: res.status(),
          method: res.request().method(),
        })
      }
    })

    // ── Preflight API surface (backend only) ──────────────────────────
    console.log('\n=== API preflight ===')
    const apiChecks: Array<{ method: string; path: string; body?: unknown; group: string }> = [
      { method: 'GET', path: '/status', group: 'health' },
      { method: 'GET', path: '/state', group: 'observe' },
      { method: 'GET', path: '/history?limit=20', group: 'observe' },
      { method: 'GET', path: '/topology', group: 'observe' },
      { method: 'GET', path: '/settings', group: 'settings' },
      { method: 'GET', path: '/sessions', group: 'session' },
      { method: 'GET', path: '/sessions/profiles', group: 'session' },
      { method: 'GET', path: '/control/status', group: 'control' },
      { method: 'GET', path: '/protocol/dictionary', group: 'protocol' },
      { method: 'GET', path: '/protocol/messages', group: 'protocol' },
      { method: 'GET', path: '/events?limit=10', group: 'diag' },
      { method: 'GET', path: '/episodes', group: 'diag' },
      { method: 'GET', path: '/recordings', group: 'diag' },
      { method: 'GET', path: '/logs?limit=20', group: 'logs' },
      { method: 'GET', path: '/logs/stats', group: 'logs' },
      { method: 'GET', path: '/synthetic-peers', group: 'synthetic' },
      { method: 'GET', path: '/tests', group: 'tests' },
      // GET /injections is not implemented (POST only) — skip list
    ]
    for (const c of apiChecks) {
      const r = await probeApi(request, c.method, c.path, c.body)
      note({
        tab: 'api',
        control: `${c.method} ${c.path}`,
        severity: r.ok || r.status === 404 ? (r.ok ? 'ok' : 'warn') : 'error',
        detail: r.ok ? `HTTP ${r.status}` : `HTTP ${r.status} ${r.snippet}`,
        http: r.status,
        apiPath: c.path,
      })
    }

    // ── Open UI ───────────────────────────────────────────────────────
    console.log('\n=== Shell ===')
    await page.goto('/')
    await expect(page.getByTestId('app')).toBeVisible({ timeout: 25_000 })
    // Wait for stream to settle
    await page.waitForTimeout(3000)

    const health = await textOf(page, 'chip-health-overall')
    const stream = await textOf(page, 'chip-stream')
    note({
      tab: 'shell',
      control: 'health-strip',
      severity: /offline|fault/i.test(health) || /lost/i.test(stream) ? 'error' : 'ok',
      detail: `health=${health || '?'} stream=${stream || '?'}`,
    })

    // Topbar mode toggle present
    if (await visible(page, 'topbar-mode-toggle')) {
      const computerOn = await page.getByTestId('topbar-mode-computer').getAttribute('aria-pressed')
      note({
        tab: 'shell',
        control: 'mode-toggle',
        severity: 'ok',
        detail: `Computer/Real toggle visible; computer pressed=${computerOn}`,
      })
    } else {
      note({
        tab: 'shell',
        control: 'mode-toggle',
        severity: 'error',
        detail: 'topbar-mode-toggle missing',
      })
    }

    // ESTOP button present (click carefully — fires inject)
    if (await visible(page, 'btn-header-estop')) {
      note({ tab: 'shell', control: 'header-estop', severity: 'ok', detail: 'E-STOP control present' })
    } else {
      note({
        tab: 'shell',
        control: 'header-estop',
        severity: 'error',
        detail: 'Header ESTOP missing',
      })
    }

    // Sidebar nav items
    const navIds = [
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
    ]
    for (const id of navIds) {
      const tid = `nav-${id}`
      const ok = await visible(page, tid)
      note({
        tab: 'shell',
        control: tid,
        severity: ok ? 'ok' : 'error',
        detail: ok ? 'nav item visible' : 'nav item MISSING',
      })
    }
    await page.screenshot({ path: path.join(OUT, '00-shell.png'), fullPage: true })

    // ── Overview ──────────────────────────────────────────────────────
    console.log('\n=== Overview ===')
    await go(page, 'overview')
    const ovChecks = [
      'workspace-overview',
      'safety-strip',
      'overview-meters',
      'cmd-feedback',
      'status-power',
      'status-mode',
      'strip-brake',
    ]
    for (const id of ovChecks) {
      const ok = await visible(page, id)
      note({
        tab: 'overview',
        control: id,
        severity: ok ? 'ok' : 'error',
        detail: ok ? 'visible' : 'not found',
      })
    }
    // Continuous meters should update from stream
    const speedSidebar = await textOf(page, 'sidebar-speed')
    note({
      tab: 'overview',
      control: 'sidebar-speed',
      severity: speedSidebar && speedSidebar !== '—' ? 'ok' : 'warn',
      detail: `sidebar speed="${speedSidebar || '(empty)'}" (stream-driven)`,
    })
    await page.screenshot({ path: path.join(OUT, '01-overview.png'), fullPage: true })

    // ── Network ───────────────────────────────────────────────────────
    console.log('\n=== Network ===')
    await go(page, 'network')
    for (const id of ['workspace-network', 'bus-health', 'topology-map']) {
      const ok = await visible(page, id)
      note({
        tab: 'network',
        control: id,
        severity: ok ? 'ok' : 'error',
        detail: ok ? 'visible' : 'not found',
      })
    }
    // Topology nodes — try click first node if any
    const nodes = page.locator('[data-testid^="node-"]')
    const nCount = await nodes.count()
    note({
      tab: 'network',
      control: 'topology-nodes',
      severity: nCount > 0 ? 'ok' : 'warn',
      detail: `${nCount} topology node(s)`,
    })
    if (nCount > 0) {
      await nodes.first().click()
      await page.waitForTimeout(300)
      note({
        tab: 'network',
        control: 'node-click',
        severity: 'ok',
        detail: 'clicked first topology node',
      })
    }
    await page.screenshot({ path: path.join(OUT, '02-network.png'), fullPage: true })

    // ── Live CAN ──────────────────────────────────────────────────────
    console.log('\n=== Live CAN ===')
    await go(page, 'live')
    for (const id of [
      'workspace-live',
      'live-filter',
      'live-view-mode',
      'live-can-table',
    ]) {
      const ok = await visible(page, id)
      note({
        tab: 'live',
        control: id,
        severity: ok ? 'ok' : 'error',
        detail: ok ? 'visible' : 'not found',
      })
    }

    await page.getByTestId('live-mode-chrono').click()
    await expect(page.getByTestId('live-chrono-table')).toBeVisible({ timeout: 8_000 })
    note({ tab: 'live', control: 'chrono-mode', severity: 'ok', detail: 'chrono table shown' })

    // Pause only exists in chrono mode
    if (await visible(page, 'live-pause')) {
      await page.getByTestId('live-pause').click()
      await page.waitForTimeout(200)
      await page.getByTestId('live-pause').click()
      note({ tab: 'live', control: 'pause', severity: 'ok', detail: 'pause toggled in chrono mode' })
    } else {
      note({
        tab: 'live',
        control: 'pause',
        severity: 'warn',
        detail: 'live-pause not visible even in chrono mode',
      })
    }

    await page.getByTestId('live-mode-latest').click()
    await page.getByTestId('filter-bus-high').click()
    await page.getByTestId('filter-bus-low').click()
    await page.getByTestId('filter-bus-both').click()
    note({ tab: 'live', control: 'bus-filters', severity: 'ok', detail: 'high/low/both toggled' })

    // Click a message row if present
    const rows = page.locator('[data-testid^="row-"]')
    const rCount = await rows.count()
    if (rCount > 0) {
      await rows.first().click()
      const detailOk = await visible(page, 'live-detail')
      note({
        tab: 'live',
        control: 'row-detail',
        severity: detailOk ? 'ok' : 'warn',
        detail: detailOk
          ? `detail drawer open after row click (${rCount} rows)`
          : `row clicked but live-detail not visible (${rCount} rows)`,
      })
    } else {
      note({
        tab: 'live',
        control: 'message-rows',
        severity: 'warn',
        detail: 'no CAN rows yet (empty latest table)',
      })
    }
    await page.screenshot({ path: path.join(OUT, '03-live.png'), fullPage: true })

    // ── Control ───────────────────────────────────────────────────────
    console.log('\n=== Control ===')
    await go(page, 'control')
    await expect(page.getByTestId('control-session-panel')).toBeVisible()

    // Enable Bench TX
    const benchBefore = await textOf(page, 'control-bench-tx')
    if (await visible(page, 'btn-enable-tx')) {
      await page.getByTestId('btn-enable-tx').click()
      await page.waitForTimeout(800)
    }
    const benchAfter = await textOf(page, 'control-bench-tx')
    const benchOn = /on|enabled/i.test(benchAfter)
    note({
      tab: 'control',
      control: 'bench-tx-enable',
      severity: benchOn ? 'ok' : 'error',
      detail: `before="${benchBefore}" after="${benchAfter}"`,
    })
    if (benchOn) {
      const dis = await visible(page, 'btn-disable-tx')
      note({
        tab: 'control',
        control: 'bench-tx-disable-btn',
        severity: dis ? 'ok' : 'warn',
        detail: dis ? 'Disable TX button visible when ON' : 'Disable TX missing when ON',
      })
    }

    // High method inject
    await page.getByTestId('control-method-high').click()
    await expect(page.getByTestId('high-analysis-inject')).toBeVisible()
    if (await page.getByTestId('check-periodic').isChecked()) {
      await page.getByTestId('check-periodic').uncheck()
    }
    await page.getByTestId('input-speed').fill('420')
    await page.getByTestId('input-yaw').fill('80')
    await page.getByTestId('input-gear').selectOption('1')
    await page.getByTestId('btn-inject-drive').click()
    await page.waitForTimeout(600)
    const clog1 = await textOf(page, 'control-log')
    note({
      tab: 'control',
      control: 'high-host-drive-inject',
      severity: /HOST_DRIVE|inject|oneshot|submitted|ok|job/i.test(clog1) ? 'ok' : 'error',
      detail: clog1.slice(0, 200) || '(empty control log)',
    })

    // Keyboard enable
    if (await visible(page, 'btn-kb-enable')) {
      await page.getByTestId('btn-kb-enable').click()
      await page.waitForTimeout(500)
      const kbBanner = await visible(page, 'kb-active-banner')
      note({
        tab: 'control',
        control: 'keyboard-enable',
        severity: kbBanner || /keyboard|kb|armed|active/i.test(await textOf(page, 'control-log'))
          ? 'ok'
          : 'warn',
        detail: kbBanner
          ? 'keyboard active banner shown'
          : `kb banner missing; log=${(await textOf(page, 'control-log')).slice(0, 120)}`,
      })
    }

    // Low method direct actuators
    await page.getByTestId('control-method-low').click()
    await expect(page.getByTestId('direct-actuators')).toBeVisible()
    await expect(page.getByTestId('direct-safety-banner')).toBeVisible()
    note({
      tab: 'control',
      control: 'low-safety-banner',
      severity: 'ok',
      detail: 'Low-bus safety bypass banner visible',
    })

    await page.getByTestId('btn-direct-motor-start').click()
    await page.waitForTimeout(900)
    const motorTx = await textOf(page, 'direct-motor-tx')
    note({
      tab: 'control',
      control: 'direct-motor-start',
      severity: /speed=\d|0x|tx|streaming|frame/i.test(motorTx) ||
        /motor|direct|channel|start/i.test(await textOf(page, 'control-log'))
        ? 'ok'
        : 'error',
      detail: `motor-tx="${motorTx.slice(0, 120)}"`,
    })
    await page.getByTestId('btn-direct-motor-stop').click()
    await page.waitForTimeout(400)

    await page.getByTestId('btn-direct-steer-start').click()
    await page.waitForTimeout(700)
    const steerTx = await textOf(page, 'direct-steer-tx')
    note({
      tab: 'control',
      control: 'direct-steer-start',
      severity: steerTx.length > 0 ? 'ok' : 'warn',
      detail: `steer-tx="${steerTx.slice(0, 100)}"`,
    })
    await page.getByTestId('btn-direct-steer-stop').click()

    await page.getByTestId('btn-direct-brake-start').click()
    await page.waitForTimeout(700)
    const brakeTx = await textOf(page, 'direct-brake-tx')
    note({
      tab: 'control',
      control: 'direct-brake-start',
      severity: brakeTx.length > 0 ? 'ok' : 'warn',
      detail: `brake-tx="${brakeTx.slice(0, 100)}"`,
    })
    await page.getByTestId('btn-direct-brake-stop').click()

    // HMI method
    await page.getByTestId('control-method-hmi').click()
    await expect(page.getByTestId('hmi-panel')).toBeVisible()
    await page.getByTestId('btn-mode-auto').click()
    await page.waitForTimeout(400)
    await page.getByTestId('btn-power-on').click()
    await page.waitForTimeout(400)
    const hmiKv = await textOf(page, 'hmi-requested-confirmed')
    note({
      tab: 'control',
      control: 'hmi-mode-power',
      severity: hmiKv.length > 0 ? 'ok' : 'warn',
      detail: `hmi kv="${hmiKv.slice(0, 160)}"`,
    })

    // Stop all
    if (await visible(page, 'btn-stop-all')) {
      await page.getByTestId('btn-stop-all').click()
      await page.waitForTimeout(500)
      note({
        tab: 'control',
        control: 'stop-all',
        severity: 'ok',
        detail: 'Stop all clicked',
      })
    }

    // Open Drive handoff
    if (await visible(page, 'btn-open-drive-from-control')) {
      note({
        tab: 'control',
        control: 'open-drive-link',
        severity: 'ok',
        detail: 'Open Drive control present',
      })
    }

    await page.screenshot({ path: path.join(OUT, '04-control.png'), fullPage: true })

    // ── Drive ─────────────────────────────────────────────────────────
    console.log('\n=== Drive ===')
    await go(page, 'preview')
    await expect(page.getByTestId('workspace-preview')).toBeVisible()

    // Common Drive testids from DriveConsole
    const driveCandidates = [
      'drive-console',
      'btn-drive-arm',
      'btn-drive-disarm',
      'drive-status',
      'drive-keycaps',
      'drive-speed-display',
      'drive-steer-display',
      'btn-drive-estop',
      'vehicle-preview',
    ]
    const foundDrive: string[] = []
    const missingDrive: string[] = []
    for (const id of driveCandidates) {
      if (await visible(page, id)) foundDrive.push(id)
      else missingDrive.push(id)
    }
    note({
      tab: 'drive',
      control: 'testid-inventory',
      severity: foundDrive.length >= 2 ? 'ok' : 'warn',
      detail: `found=[${foundDrive.join(',')}] missing=[${missingDrive.join(',')}]`,
    })

    // Ensure bench TX, then arm Drive
    await go(page, 'control')
    if (await visible(page, 'btn-enable-tx')) {
      await page.getByTestId('btn-enable-tx').click({ timeout: 5_000 }).catch(() => undefined)
      await page.waitForTimeout(400)
    }
    await go(page, 'preview')
    if (await visible(page, 'btn-drive-arm')) {
      await page.getByTestId('btn-drive-arm').click({ timeout: 5_000 })
      await page.waitForTimeout(900)
      const armChip = await textOf(page, 'drive-arm-chip')
      const armed =
        (await visible(page, 'btn-drive-disarm')) || /armed|on|yes/i.test(armChip)
      const driveLog = await textOf(page, 'drive-log')
      note({
        tab: 'drive',
        control: 'arm',
        severity: armed ? 'ok' : 'error',
        detail: armed
          ? `armed chip="${armChip}"`
          : `arm failed chip="${armChip}" log=${driveLog.slice(0, 140)}`,
      })
      // Keycap click while armed
      if (armed && (await visible(page, 'keycap-W'))) {
        await page.getByTestId('keycap-W').click({ timeout: 3_000 })
        await page.waitForTimeout(400)
        note({
          tab: 'drive',
          control: 'keycap-W',
          severity: 'ok',
          detail: 'W keycap clicked while armed',
        })
      }
      if (await visible(page, 'btn-drive-disarm')) {
        await page.getByTestId('btn-drive-disarm').click({ timeout: 5_000 })
        await page.waitForTimeout(400)
        note({ tab: 'drive', control: 'disarm', severity: 'ok', detail: 'disarmed' })
      }
    } else {
      note({
        tab: 'drive',
        control: 'arm',
        severity: 'error',
        detail: 'btn-drive-arm not visible',
      })
    }
    await page.screenshot({ path: path.join(OUT, '05-drive.png'), fullPage: true })

    // ── Bench ─────────────────────────────────────────────────────────
    console.log('\n=== Bench ===')
    await go(page, 'bench')
    const benchBody = await textOf(page, 'workspace-bench')
    note({
      tab: 'bench',
      control: 'workspace-content',
      severity: /setup|profile|bench/i.test(benchBody) ? 'ok' : 'warn',
      detail: `content length=${benchBody.length}`,
    })
    // Check for interactive controls (buttons)
    const benchButtons = page.locator('[data-testid="workspace-bench"] button')
    const bb = await benchButtons.count()
    note({
      tab: 'bench',
      control: 'interactive-actions',
      severity: bb === 0 ? 'missing' : 'ok',
      detail:
        bb === 0
          ? 'NO action buttons — read-only checklist (synthetic peers / ECU setup not clickable)'
          : `${bb} button(s)`,
    })
    // Cross-check synthetic API exists but UI missing
    const syn = await probeApi(request, 'GET', '/synthetic-peers')
    note({
      tab: 'bench',
      control: 'synthetic-peers-api',
      severity: syn.ok ? 'missing' : 'warn',
      detail: syn.ok
        ? `API OK (HTTP ${syn.status}) but Bench UI has no Start/Stop synthetic peers controls`
        : `synthetic-peers API HTTP ${syn.status}`,
      http: syn.status,
      apiPath: '/synthetic-peers',
    })
    await page.screenshot({ path: path.join(OUT, '06-bench.png'), fullPage: true })

    // ── Dictionary ────────────────────────────────────────────────────
    console.log('\n=== Dictionary ===')
    await go(page, 'dictionary')
    await expect(page.getByTestId('workspace-dictionary')).toBeVisible({ timeout: 12_000 })
    await page.waitForTimeout(1500)
    const dictBody = await textOf(page, 'workspace-dictionary')
    note({
      tab: 'dictionary',
      control: 'workspace',
      severity: dictBody.length > 50 ? 'ok' : 'error',
      detail: `content length=${dictBody.length}`,
    })

    // Search / filter if present
    const search = page.locator(
      '[data-testid="workspace-dictionary"] input[type="search"], [data-testid="workspace-dictionary"] input[placeholder*="earch" i], [data-testid="dict-search"]',
    )
    if (await search.count()) {
      await search.first().fill('HOST')
      await page.waitForTimeout(400)
      note({ tab: 'dictionary', control: 'search', severity: 'ok', detail: 'typed HOST into search' })
    } else {
      note({
        tab: 'dictionary',
        control: 'search',
        severity: 'warn',
        detail: 'no search input found',
      })
    }

    // Expand first message card
    const expandBtn = page.locator(
      '[data-testid="workspace-dictionary"] button, [data-testid^="dict-msg-"]',
    )
    const eCount = await expandBtn.count()
    if (eCount > 0) {
      await expandBtn.first().click()
      await page.waitForTimeout(500)
      note({
        tab: 'dictionary',
        control: 'expand-message',
        severity: 'ok',
        detail: `clicked expand-like control (${eCount} candidates)`,
      })
    } else {
      note({
        tab: 'dictionary',
        control: 'expand-message',
        severity: 'warn',
        detail: 'no expandable message controls found',
      })
    }

    // Refresh dictionary if button exists
    const refreshDict = page.getByRole('button', { name: /refresh/i })
    if (await refreshDict.count()) {
      await refreshDict.first().click()
      await page.waitForTimeout(800)
      note({ tab: 'dictionary', control: 'refresh', severity: 'ok', detail: 'refresh clicked' })
    }
    await page.screenshot({ path: path.join(OUT, '07-dictionary.png'), fullPage: true })

    // ── Diagnostics ───────────────────────────────────────────────────
    console.log('\n=== Diagnostics ===')
    await go(page, 'diagnostics')
    for (const id of [
      'workspace-diagnostics',
      'recording-panel',
      'episodes-panel',
      'events-panel',
      'btn-diag-refresh',
      'btn-rec-start',
      'btn-rec-stop',
    ]) {
      const ok = await visible(page, id)
      note({
        tab: 'diagnostics',
        control: id,
        severity: ok ? 'ok' : 'error',
        detail: ok ? 'visible' : 'not found',
      })
    }

    await page.getByTestId('btn-diag-refresh').click()
    await page.waitForTimeout(500)
    note({ tab: 'diagnostics', control: 'refresh-click', severity: 'ok', detail: 'refresh clicked' })

    // Start + stop recording
    await page.getByTestId('btn-rec-start').click()
    await page.waitForTimeout(1200)
    const recState = await textOf(page, 'diag-recording')
    const recLog = await textOf(page, 'recording-log')
    note({
      tab: 'diagnostics',
      control: 'recording-start',
      severity: /on/i.test(recState) || /started|recording/i.test(recLog) ? 'ok' : 'error',
      detail: `recording=${recState} log=${recLog.slice(0, 120)}`,
    })
    await page.getByTestId('btn-rec-stop').click()
    await page.waitForTimeout(800)
    const recLog2 = await textOf(page, 'recording-log')
    note({
      tab: 'diagnostics',
      control: 'recording-stop',
      severity: /stopped|frames|quality|off/i.test(recLog2 + (await textOf(page, 'diag-recording')))
        ? 'ok'
        : 'warn',
      detail: `log=${recLog2.slice(0, 140)}`,
    })

    // Evidence open if any recording row
    const evidenceBtns = page.locator('[data-testid^="btn-evidence-"]')
    const evN = await evidenceBtns.count()
    if (evN > 0) {
      await evidenceBtns.first().click()
      await page.waitForTimeout(600)
      const win = await visible(page, 'evidence-window')
      note({
        tab: 'diagnostics',
        control: 'evidence-open',
        severity: win ? 'ok' : 'warn',
        detail: win ? 'evidence window open' : 'evidence button clicked but window not shown',
      })
    } else {
      note({
        tab: 'diagnostics',
        control: 'evidence-open',
        severity: 'warn',
        detail: 'no evidence buttons (no completed recordings listed)',
      })
    }

    // Export recording — API exists, UI?
    const exportBtn = page.getByRole('button', { name: /export/i })
    note({
      tab: 'diagnostics',
      control: 'recording-export',
      severity: (await exportBtn.count()) > 0 ? 'ok' : 'missing',
      detail:
        (await exportBtn.count()) > 0
          ? 'export control present'
          : 'GET /recordings/{id}/export exists in API but no Export button in UI',
      apiPath: '/recordings/{id}/export',
    })

    // Tests API — no UI runner?
    const testsApi = await probeApi(request, 'GET', '/tests')
    const testRunner = page.getByRole('button', { name: /run test|verification/i })
    note({
      tab: 'diagnostics',
      control: 'test-runner',
      severity: (await testRunner.count()) > 0 ? 'ok' : 'missing',
      detail:
        (await testRunner.count()) > 0
          ? 'test runner present'
          : `POST/GET /tests API HTTP ${testsApi.status} but no test-runner UI on Diagnostics`,
      http: testsApi.status,
      apiPath: '/tests',
    })
    await page.screenshot({ path: path.join(OUT, '08-diagnostics.png'), fullPage: true })

    // ── Logging ───────────────────────────────────────────────────────
    console.log('\n=== Logging ===')
    await go(page, 'logs')
    for (const id of [
      'workspace-logs',
      'logs-category',
      'logs-severity',
      'logs-filter',
      'logs-refresh',
    ]) {
      const ok = await visible(page, id)
      note({
        tab: 'logs',
        control: id,
        severity: ok ? 'ok' : 'error',
        detail: ok ? 'visible' : 'not found',
      })
    }
    await page.getByTestId('logs-refresh').click()
    await page.waitForTimeout(500)
    if (await visible(page, 'logs-filter')) {
      await page.getByTestId('logs-filter').fill('session')
      await page.waitForTimeout(300)
    }
    if (await visible(page, 'logs-category')) {
      // try select first non-empty option if select
      const cat = page.getByTestId('logs-category')
      const tag = await cat.evaluate((el) => el.tagName).catch(() => '')
      if (tag === 'SELECT') {
        const opts = await cat.locator('option').allTextContents()
        if (opts.length > 1) await cat.selectOption({ index: 1 })
      }
    }
    note({ tab: 'logs', control: 'filters-exercise', severity: 'ok', detail: 'filters exercised' })

    // clear logs?
    const clearBtn = page.getByRole('button', { name: /clear/i })
    note({
      tab: 'logs',
      control: 'clear-logs',
      severity: (await clearBtn.count()) > 0 ? 'ok' : 'missing',
      detail:
        (await clearBtn.count()) > 0
          ? 'clear control present'
          : 'DELETE /logs exists in api.ts but no Clear button found',
      apiPath: '/logs',
    })
    // log detail by id?
    note({
      tab: 'logs',
      control: 'log-detail-by-id',
      severity: 'missing',
      detail: 'GET /logs/{log_id} and /logs/stats not surfaced as dedicated UI panels',
      apiPath: '/logs/{log_id}',
    })
    await page.screenshot({ path: path.join(OUT, '09-logs.png'), fullPage: true })

    // ── Settings ──────────────────────────────────────────────────────
    console.log('\n=== Settings ===')
    await go(page, 'settings')
    await expect(page.getByTestId('workspace-settings')).toBeVisible()
    const settingsText = await textOf(page, 'workspace-settings')
    note({
      tab: 'settings',
      control: 'settings-snapshot',
      severity: /settings|transport|session|protocol/i.test(settingsText) ? 'ok' : 'error',
      detail: `content length=${settingsText.length}`,
    })

    // Computer / Real mode buttons in settings if present
    const modeBtns = page.locator(
      '[data-testid="workspace-settings"] button, [data-testid^="settings-mode-"]',
    )
    const mCount = await modeBtns.count()
    note({
      tab: 'settings',
      control: 'settings-buttons',
      severity: mCount > 0 ? 'ok' : 'warn',
      detail: `${mCount} button(s) in settings workspace`,
    })

    // Click refresh settings if present
    const settingsRefresh = page.getByRole('button', { name: /refresh/i })
    if (await settingsRefresh.count()) {
      await settingsRefresh.first().click()
      await page.waitForTimeout(600)
      note({ tab: 'settings', control: 'refresh', severity: 'ok', detail: 'settings refresh clicked' })
    }

    // Profile / transport switch
    const computerBtn = page.getByTestId('topbar-mode-computer')
    const realBtn = page.getByTestId('topbar-mode-real')
    if (await computerBtn.isVisible()) {
      await computerBtn.click()
      await page.waitForTimeout(1000)
      note({
        tab: 'settings',
        control: 'topbar-computer-mode',
        severity: 'ok',
        detail: 'Computer mode re-selected',
      })
    }
    // Do NOT switch to Real without adapter — just check availability of Real button
    if (await realBtn.isVisible()) {
      const disabled = await realBtn.isDisabled().catch(() => false)
      note({
        tab: 'settings',
        control: 'topbar-real-mode',
        severity: 'ok',
        detail: `Real mode button visible; disabled=${disabled} (click skipped — needs CANalyst)`,
      })
    }

    // Lease UI missing?
    const leaseBtn = page.getByRole('button', { name: /lease|claim|release ownership/i })
    note({
      tab: 'settings',
      control: 'lease-management',
      severity: (await leaseBtn.count()) > 0 ? 'ok' : 'missing',
      detail:
        (await leaseBtn.count()) > 0
          ? 'lease controls present'
          : 'leases shown as read-only list; claim/renew/release API not exposed as controls',
      apiPath: '/sessions/{id}/leases',
    })

    // Close session?
    const closeSess = page.getByRole('button', { name: /close session|end session/i })
    note({
      tab: 'settings',
      control: 'close-session',
      severity: (await closeSess.count()) > 0 ? 'ok' : 'missing',
      detail:
        (await closeSess.count()) > 0
          ? 'close session present'
          : 'DELETE /sessions/{id} in api.ts but no Close Session button found',
      apiPath: '/sessions/{id}',
    })

    // Generic injection UI
    note({
      tab: 'settings',
      control: 'generic-injection-ui',
      severity: 'missing',
      detail:
        'POST /injections + /injections/preview exist; UI only has ESTOP + HostDrive + Direct + HMI, no free-form inject panel',
      apiPath: '/injections',
    })

    await page.screenshot({ path: path.join(OUT, '10-settings.png'), fullPage: true })

    // ── Header ESTOP click (real inject) ──────────────────────────────
    console.log('\n=== Header ESTOP ===')
    await page.getByTestId('btn-header-estop').click()
    await page.waitForTimeout(800)
    const estopChip = await textOf(page, 'chip-estop')
    note({
      tab: 'shell',
      control: 'estop-click',
      severity: /active|on|true|estop/i.test(estopChip) || failedRequests.length === 0 ? 'ok' : 'warn',
      detail: `chip-estop="${estopChip}" after click`,
    })

    // ── Final stream health ───────────────────────────────────────────
    await page.waitForTimeout(1500)
    const healthEnd = await textOf(page, 'chip-health-overall')
    const streamEnd = await textOf(page, 'chip-stream')
    note({
      tab: 'shell',
      control: 'health-end',
      severity: /offline|fault/i.test(healthEnd) || /lost/i.test(streamEnd) ? 'error' : 'ok',
      detail: `end health=${healthEnd} stream=${streamEnd}`,
    })

    // Failed API requests during UI session
    const uniqueFails = new Map<string, number>()
    for (const f of failedRequests) {
      const k = `${f.method} ${f.url} → ${f.status}`
      uniqueFails.set(k, (uniqueFails.get(k) || 0) + 1)
    }
    for (const [k, n] of uniqueFails) {
      note({
        tab: 'network-errors',
        control: 'failed-request',
        severity: 'error',
        detail: `${k} (×${n})`,
      })
    }
    if (pageErrors.length) {
      note({
        tab: 'runtime',
        control: 'pageerror',
        severity: 'error',
        detail: pageErrors.slice(0, 5).join(' | '),
      })
    } else {
      note({ tab: 'runtime', control: 'pageerror', severity: 'ok', detail: 'no page errors' })
    }

    // ── Write report ──────────────────────────────────────────────────
    const summary = {
      ok: findings.filter((f) => f.severity === 'ok').length,
      warn: findings.filter((f) => f.severity === 'warn').length,
      error: findings.filter((f) => f.severity === 'error').length,
      missing: findings.filter((f) => f.severity === 'missing').length,
    }
    const report = {
      generatedAt: new Date().toISOString(),
      baseURL: 'http://127.0.0.1:5173',
      api: 'http://127.0.0.1:8001',
      summary,
      pageErrors,
      consoleErrors: consoleErrors.slice(0, 30),
      failedRequests: [...uniqueFails.entries()].map(([k, n]) => ({ key: k, count: n })),
      findings,
    }
    fs.writeFileSync(path.join(OUT, 'report.json'), JSON.stringify(report, null, 2))
    const md = renderMarkdown(report)
    fs.writeFileSync(path.join(OUT, 'REPORT.md'), md)
    console.log('\n=== SUMMARY ===')
    console.log(JSON.stringify(summary, null, 2))
    console.log(`Report: ${path.join(OUT, 'REPORT.md')}`)

    // Soft assert: shell must not be offline
    expect(summary.error, `errors found — see ${OUT}/REPORT.md`).toBeLessThan(20)
  })
})

function renderMarkdown(report: {
  generatedAt: string
  summary: Record<string, number>
  findings: Finding[]
  pageErrors: string[]
  failedRequests: Array<{ key: string; count: number }>
}): string {
  const byTab = new Map<string, Finding[]>()
  for (const f of report.findings) {
    const list = byTab.get(f.tab) || []
    list.push(f)
    byTab.set(f.tab, list)
  }
  let md = `# Live UI click audit\n\n`
  md += `Generated: ${report.generatedAt}\n\n`
  md += `## Summary\n\n`
  md += `| Severity | Count |\n|---|---|\n`
  for (const [k, v] of Object.entries(report.summary)) {
    md += `| ${k} | ${v} |\n`
  }
  md += `\n## Per tab\n\n`
  for (const [tab, list] of byTab) {
    md += `### ${tab}\n\n`
    md += `| Control | Severity | Detail |\n|---|---|---|\n`
    for (const f of list) {
      const d = f.detail.replace(/\|/g, '\\|').replace(/\n/g, ' ')
      md += `| ${f.control} | **${f.severity}** | ${d} |\n`
    }
    md += `\n`
  }
  if (report.failedRequests.length) {
    md += `## Failed HTTP during UI\n\n`
    for (const f of report.failedRequests) md += `- ${f.key} (×${f.count})\n`
  }
  if (report.pageErrors.length) {
    md += `\n## Page errors\n\n`
    for (const e of report.pageErrors) md += `- ${e}\n`
  }
  return md
}
