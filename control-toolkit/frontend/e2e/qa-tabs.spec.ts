/**
 * Automated QA: every sidebar tab + shell + interactions + console/page errors.
 * Produces screenshots under test-results/qa-tabs/ and a JSON report.
 */
import { test, expect, type Page, type ConsoleMessage } from '@playwright/test'
import fs from 'node:fs'
import path from 'node:path'

const OUT = path.join('test-results', 'qa-tabs')
const REPORT_PATH = path.join(OUT, 'qa-report.json')

type Issue = {
  severity: 'error' | 'warn' | 'info'
  tab: string
  message: string
}

type TabResult = {
  id: string
  ok: boolean
  durationMs: number
  issues: Issue[]
  screenshot: string
}

const TABS: Array<{
  id: string
  nav: string
  workspace: string
  /** Required visible markers after open */
  mustSee: string[]
  /** Optional interactive checks */
  interact?: (page: Page, issues: Issue[]) => Promise<void>
}> = [
  {
    id: 'overview',
    nav: 'nav-overview',
    workspace: 'workspace-overview',
    mustSee: ['safety-strip'],
  },
  {
    id: 'network',
    nav: 'nav-network',
    workspace: 'workspace-network',
    mustSee: ['topology-map'],
  },
  {
    id: 'live',
    nav: 'nav-live',
    workspace: 'workspace-live',
    mustSee: ['live-can-table', 'live-filter'],
    interact: async (page, issues) => {
      await page.getByTestId('live-mode-chrono').click()
      await expect(page.getByTestId('live-chrono-table')).toBeVisible({ timeout: 8_000 })
      await page.getByTestId('live-mode-latest').click()
      await expect(page.getByTestId('live-can-table')).toBeVisible()
      await page.getByTestId('live-filter').fill('HOST')
      await page.getByTestId('live-filter').fill('')
      const table = page.getByTestId('live-can-table')
      const text = (await table.innerText().catch(() => '')) || ''
      if (!text.trim()) {
        issues.push({
          severity: 'info',
          tab: 'live',
          message: 'Live CAN table empty (no traffic yet — ok on idle virtual bus)',
        })
      }
    },
  },
  {
    id: 'control',
    nav: 'nav-control',
    workspace: 'workspace-control',
    mustSee: ['control-method-picker', 'control-log'],
    interact: async (page, issues) => {
      // High method
      await page.getByTestId('control-method-high').click()
      await expect(page.getByTestId('keyboard-control')).toBeVisible()
      await expect(page.getByTestId('high-analysis-inject')).toBeVisible()
      await expect(page.getByTestId('input-yaw')).toBeVisible()
      await expect(page.getByTestId('btn-inject-drive')).toBeVisible()
      await expect(page.getByTestId('btn-enable-tx')).toBeVisible()
      await expect(page.getByTestId('btn-stop-all')).toBeVisible()

      // Low method
      await page.getByTestId('control-method-low').click()
      await expect(page.getByTestId('direct-actuators')).toBeVisible()
      await expect(page.getByTestId('direct-motor')).toBeVisible()
      await expect(page.getByTestId('direct-steering')).toBeVisible()
      await expect(page.getByTestId('direct-brake')).toBeVisible()

      // HMI method
      await page.getByTestId('control-method-hmi').click()
      await expect(page.getByTestId('hmi-panel')).toBeVisible()
      await expect(page.getByTestId('btn-mode-manual')).toBeVisible()
      await expect(page.getByTestId('btn-power-on')).toBeVisible()

      // Back to high + enable TX
      await page.getByTestId('control-method-high').click()
      await page.getByTestId('btn-enable-tx').click()
      await expect(page.getByTestId('control-log')).toContainText(/Bench TX|enabled|Method/i, {
        timeout: 12_000,
      })

      await page.getByTestId('input-speed').fill('111')
      await page.getByTestId('input-yaw').fill('22')
      await page.getByTestId('check-periodic').uncheck()
      await page.getByTestId('btn-inject-drive').click()
      await expect(page.getByTestId('control-log')).toContainText(
        /HOST_DRIVE|submitted|oneshot|High-bus|inject/i,
        { timeout: 12_000 },
      )

      // Method badge present
      const method = page.getByTestId('control-active-method')
      if (!(await method.isVisible().catch(() => false))) {
        issues.push({
          severity: 'warn',
          tab: 'control',
          message: 'control-active-method badge not visible',
        })
      }
    },
  },
  {
    id: 'preview',
    nav: 'nav-preview',
    workspace: 'workspace-preview',
    mustSee: ['preview-canvas', 'preview-telemetry', 'drive-keycaps', 'btn-drive-arm'],
    interact: async (page) => {
      await expect(page.getByTestId('preview-mode-adaptive')).toBeVisible()
      await expect(page.getByTestId('preview-mode-direct')).toBeVisible()
      await page.getByTestId('preview-mode-direct').click()
      await expect(page.getByTestId('preview-mode-blurb')).toContainText(/Direct/i)
      await page.getByTestId('preview-mode-adaptive').click()
      await page.getByTestId('btn-drive-arm').click()
      await expect(page.getByTestId('btn-drive-disarm')).toBeVisible({ timeout: 15_000 })
      await expect(page.getByTestId('drive-log')).toContainText(/Armed|HOST_DRIVE|CAN/i, {
        timeout: 10_000,
      })
      // Canvas has non-zero size
      const box = await page.getByTestId('preview-canvas').boundingBox()
      expect(box).toBeTruthy()
      expect((box?.width ?? 0) > 100).toBeTruthy()
      expect((box?.height ?? 0) > 100).toBeTruthy()
      await page.getByTestId('btn-drive-disarm').click()
    },
  },
  {
    id: 'bench',
    nav: 'nav-bench',
    workspace: 'workspace-bench',
    mustSee: [],
    interact: async (page, issues) => {
      const text = await page.getByTestId('workspace-bench').innerText()
      if (!/Bench|profile|virtual|TX/i.test(text)) {
        issues.push({
          severity: 'error',
          tab: 'bench',
          message: 'Bench workspace missing expected content',
        })
      }
    },
  },
  {
    id: 'dictionary',
    nav: 'nav-dictionary',
    workspace: 'workspace-dictionary',
    mustSee: ['dict-filter', 'dict-grid'],
    interact: async (page, issues) => {
      await expect(page.getByTestId('dict-refresh')).toBeVisible()
      await expect(page.getByTestId('dict-bus-all')).toBeVisible()
      await expect(page.getByTestId('dict-bus-high')).toBeVisible()
      await expect(page.getByTestId('dict-bus-low')).toBeVisible()

      // Message cards from YAML catalog (old layout)
      await expect(page.locator('[data-testid="frame-row"]').first()).toBeVisible({
        timeout: 15_000,
      })
      const count = await page.locator('[data-testid="frame-row"]').count()
      if (count < 5) {
        issues.push({
          severity: 'error',
          tab: 'dictionary',
          message: `Expected many dictionary cards from YAML, got ${count}`,
        })
      }

      await page.getByTestId('dict-bus-high').click()
      await page.getByTestId('dict-filter').fill('HOST_DRIVE')
      await expect(page.locator('[data-testid="frame-row"]').first()).toBeVisible({
        timeout: 8_000,
      })
      // Bit grid + signal table always present on card
      await expect(page.getByTestId('dict-bit-grid').first()).toBeVisible()
      await expect(page.getByTestId('dict-signal-table').first()).toBeVisible()
      await expect(page.getByTestId('dict-bit-inspector').first()).toBeVisible()

      // Hover a bit cell — inspector stays fixed height (layout stability)
      const bit = page.locator('button.bit-cell.filled').first()
      if (await bit.count()) {
        const boxBefore = await page.getByTestId('dict-bit-inspector').first().boundingBox()
        await bit.hover()
        await page.waitForTimeout(80)
        const boxAfter = await page.getByTestId('dict-bit-inspector').first().boundingBox()
        if (boxBefore && boxAfter) {
          const dh = Math.abs(boxAfter.height - boxBefore.height)
          if (dh > 2) {
            issues.push({
              severity: 'error',
              tab: 'dictionary',
              message: `bit inspector height jumped on hover: ${boxBefore.height} → ${boxAfter.height}`,
            })
          }
        }
        // Click signal row expands "how bits work"
        const sigRow = page.locator('[data-testid^="dict-sig-row-"]').first()
        if (await sigRow.count()) {
          await sigRow.click()
          await expect(page.locator('[data-testid^="dict-sig-expand-"]').first()).toBeVisible({
            timeout: 5_000,
          })
        }
      }

      await page.getByTestId('dict-filter').fill('')
      await page.getByTestId('dict-bus-all').click()
      await page.getByTestId('dict-refresh').click()
      await expect(page.locator('[data-testid="frame-row"]').first()).toBeVisible({
        timeout: 15_000,
      })
    },
  },
  {
    id: 'diagnostics',
    nav: 'nav-diagnostics',
    workspace: 'workspace-diagnostics',
    mustSee: ['recording-panel', 'events-panel', 'episodes-panel'],
    interact: async (page) => {
      await expect(page.getByTestId('btn-rec-start')).toBeVisible()
      await expect(page.getByTestId('btn-rec-stop')).toBeVisible()
      await page.getByTestId('btn-rec-start').click()
      await expect(page.getByTestId('recording-log')).toContainText(/Started|rec_/i, {
        timeout: 12_000,
      })
      await page.waitForTimeout(400)
      await page.getByTestId('btn-rec-stop').click()
      await expect(page.getByTestId('recording-log')).toContainText(/Stopped|quality|frames/i, {
        timeout: 12_000,
      })
    },
  },
  {
    id: 'logs',
    nav: 'nav-logs',
    workspace: 'workspace-logs',
    mustSee: ['logs-table', 'logs-filter', 'logs-category'],
    interact: async (page) => {
      await expect(page.getByTestId('logs-refresh')).toBeVisible()
      await expect(page.getByTestId('logs-export')).toBeVisible()
      await page.getByTestId('logs-category').selectOption('system')
      await page.getByTestId('logs-refresh').click()
      await expect(page.getByTestId('logs-table')).toBeVisible()
      await page.getByTestId('logs-category').selectOption('all')
    },
  },
  {
    id: 'settings',
    nav: 'nav-settings',
    workspace: 'workspace-settings',
    mustSee: ['profile-list'],
    interact: async (page) => {
      await expect(page.getByTestId('transport-toggle')).toBeVisible()
      await expect(page.getByTestId('mode-computer')).toBeVisible()
      await expect(page.getByTestId('mode-real')).toBeVisible()
      await expect(page.getByTestId('profile-list')).toContainText(/Computer|Virtual/i)
      await expect(page.getByTestId('profile-list')).toContainText(/CANalyst|Real|Bench Test|Full Vehicle/i)
      await page.getByTestId('btn-start-pure').click()
      await expect(page.getByTestId('settings-log')).toContainText(
        /Session ses_|phase running|running|Computer|Active/i,
        {
          timeout: 12_000,
        },
      )
    },
  },
]

function attachCollectors(page: Page, issues: Issue[], tab: string) {
  const onConsole = (msg: ConsoleMessage) => {
    const type = msg.type()
    const text = msg.text()
    // Ignore noisy vite HMR / benign warnings / expected API conflict noise
    if (
      /Download the React DevTools|\[vite\]|favicon|WebSocket connection|status of 409/i.test(
        text,
      )
    ) {
      // 409 conflicts are tracked via requestfailed + UI log; browser always logs them.
      if (/status of 409/i.test(text)) {
        issues.push({
          severity: 'warn',
          tab,
          message: `HTTP 409 (conflict) — ${text.slice(0, 160)}`,
        })
      }
      return
    }
    if (type === 'error') {
      issues.push({ severity: 'error', tab, message: `console.error: ${text.slice(0, 300)}` })
    } else if (type === 'warning' && /Failed|Error|Contrast/i.test(text)) {
      issues.push({ severity: 'warn', tab, message: `console.warn: ${text.slice(0, 300)}` })
    }
  }
  const onPageError = (err: Error) => {
    issues.push({
      severity: 'error',
      tab,
      message: `pageerror: ${err.message.slice(0, 300)}`,
    })
  }
  const onRequestFailed = (req: {
    url: () => string
    failure: () => { errorText?: string } | null
  }) => {
    const url = req.url()
    if (!url.includes('/api/')) return
    const f = req.failure()
    // net::ERR_ABORTED on navigation is noise
    if (/ERR_ABORTED|NS_BINDING_ABORTED/i.test(f?.errorText || '')) return
    issues.push({
      severity: 'error',
      tab,
      message: `request failed: ${url} — ${f?.errorText || 'unknown'}`,
    })
  }
  // Capture HTTP 4xx/5xx on completed responses (not only network failures)
  const onResponse = (res: { url: () => string; status: () => number }) => {
    const url = res.url()
    if (!url.includes('/api/')) return
    const status = res.status()
    if (status >= 500) {
      issues.push({
        severity: 'error',
        tab,
        message: `HTTP ${status}: ${url}`,
      })
    } else if (status === 409) {
      issues.push({
        severity: 'warn',
        tab,
        message: `HTTP 409: ${url}`,
      })
    } else if (status >= 400) {
      issues.push({
        severity: 'warn',
        tab,
        message: `HTTP ${status}: ${url}`,
      })
    }
  }
  page.on('console', onConsole)
  page.on('pageerror', onPageError)
  page.on('requestfailed', onRequestFailed)
  page.on('response', onResponse)
  return () => {
    page.off('console', onConsole)
    page.off('pageerror', onPageError)
    page.off('requestfailed', onRequestFailed)
    page.off('response', onResponse)
  }
}

async function assertNoOverlayCrash(page: Page, tab: string, issues: Issue[]) {
  // Blank main or React crash root
  const app = page.getByTestId('app')
  if (!(await app.isVisible().catch(() => false))) {
    issues.push({ severity: 'error', tab, message: 'app root not visible' })
  }
  const bodyText = (await page.locator('body').innerText().catch(() => '')) || ''
  if (/Something went wrong|Unhandled Runtime Error|Cannot read propert/i.test(bodyText)) {
    issues.push({
      severity: 'error',
      tab,
      message: `crash/error text in body: ${bodyText.slice(0, 160)}`,
    })
  }
}

async function checkShell(page: Page, issues: Issue[]) {
  const tab = 'shell'
  for (const id of [
    'topbar',
    'sidebar',
    'health-strip',
    'chip-health-overall',
    'chip-stream',
    'chip-profile',
    'chip-high',
    'chip-low',
    'btn-header-estop',
  ]) {
    if (!(await page.getByTestId(id).isVisible().catch(() => false))) {
      issues.push({ severity: 'error', tab, message: `missing shell element: ${id}` })
    }
  }
  // Compact 2-row health bar (primary strip + meta row)
  if (!(await page.getByTestId('topbar-row-session').isVisible().catch(() => false))) {
    issues.push({ severity: 'warn', tab, message: 'missing topbar meta row: topbar-row-session' })
  }
  // Sidebar cmd strip
  for (const id of ['sidebar-cmd-strip', 'sidebar-speed', 'sidebar-steer', 'sidebar-system-card']) {
    if (!(await page.getByTestId(id).isVisible().catch(() => false))) {
      issues.push({ severity: 'warn', tab, message: `missing sidebar element: ${id}` })
    }
  }
}

test.describe('QA — every tab automated', () => {
  test.setTimeout(180_000)

  test('audit all workspaces, interactions, console errors', async ({ page }) => {
    fs.mkdirSync(OUT, { recursive: true })
    const allIssues: Issue[] = []
    const tabResults: TabResult[] = []

    const shellIssues: Issue[] = []
    const detachShell = attachCollectors(page, shellIssues, 'shell')

    await page.goto('/')
    await expect(page.getByTestId('app')).toBeVisible({ timeout: 20_000 })
    await expect(page.getByTestId('chip-stream')).not.toHaveText(/CONNECTING/i, {
      timeout: 25_000,
    })

    await checkShell(page, shellIssues)
    await page.screenshot({ path: path.join(OUT, '00-shell.png'), fullPage: true })
    detachShell()
    allIssues.push(...shellIssues)
    tabResults.push({
      id: 'shell',
      ok: !shellIssues.some((i) => i.severity === 'error'),
      durationMs: 0,
      issues: shellIssues,
      screenshot: '00-shell.png',
    })

    for (const tab of TABS) {
      const issues: Issue[] = []
      const detach = attachCollectors(page, issues, tab.id)
      const t0 = Date.now()
      try {
        await page.getByTestId(tab.nav).click()
        await expect(page.getByTestId(tab.workspace)).toBeVisible({ timeout: 12_000 })
        // Active nav state
        await expect(page.getByTestId(tab.nav)).toHaveClass(/active/)

        for (const mid of tab.mustSee) {
          await expect(page.getByTestId(mid)).toBeVisible({ timeout: 10_000 })
        }

        if (tab.interact) {
          await tab.interact(page, issues)
        }

        await assertNoOverlayCrash(page, tab.id, issues)
      } catch (e) {
        issues.push({
          severity: 'error',
          tab: tab.id,
          message: `tab failed: ${e instanceof Error ? e.message : String(e)}`.slice(0, 400),
        })
      }

      const shot = `${tab.id}.png`
      await page.screenshot({ path: path.join(OUT, shot), fullPage: true }).catch(() => undefined)
      detach()
      allIssues.push(...issues)
      tabResults.push({
        id: tab.id,
        ok: !issues.some((i) => i.severity === 'error'),
        durationMs: Date.now() - t0,
        issues,
        screenshot: shot,
      })
    }

    // Cross-tab: after control inject, overview/live should still open
    {
      const issues: Issue[] = []
      const detach = attachCollectors(page, issues, 'cross-tab')
      try {
        await page.getByTestId('nav-overview').click()
        await expect(page.getByTestId('workspace-overview')).toBeVisible()
        await page.getByTestId('nav-live').click()
        await expect(page.getByTestId('workspace-live')).toBeVisible()
        // HOST_DRIVE may exist after control inject earlier
        const live = page.getByTestId('live-can-table')
        await expect(live).toBeVisible()
      } catch (e) {
        issues.push({
          severity: 'error',
          tab: 'cross-tab',
          message: String(e).slice(0, 300),
        })
      }
      await page.screenshot({ path: path.join(OUT, 'cross-tab.png'), fullPage: true })
      detach()
      allIssues.push(...issues)
      tabResults.push({
        id: 'cross-tab',
        ok: !issues.some((i) => i.severity === 'error'),
        durationMs: 0,
        issues,
        screenshot: 'cross-tab.png',
      })
    }

    const errors = allIssues.filter((i) => i.severity === 'error')
    const warns = allIssues.filter((i) => i.severity === 'warn')
    const report = {
      generatedAt: new Date().toISOString(),
      summary: {
        tabs: tabResults.length,
        passed: tabResults.filter((t) => t.ok).length,
        failed: tabResults.filter((t) => !t.ok).length,
        errorCount: errors.length,
        warnCount: warns.length,
        infoCount: allIssues.filter((i) => i.severity === 'info').length,
      },
      tabs: tabResults,
      issues: allIssues,
    }
    fs.writeFileSync(REPORT_PATH, JSON.stringify(report, null, 2), 'utf-8')

    // Human-readable summary for CI logs
    // eslint-disable-next-line no-console
    console.log('\n=== QA TAB REPORT ===')
    // eslint-disable-next-line no-console
    console.log(JSON.stringify(report.summary, null, 2))
    if (errors.length) {
      // eslint-disable-next-line no-console
      console.log('\nERRORS:')
      for (const e of errors) {
        // eslint-disable-next-line no-console
        console.log(`  [${e.tab}] ${e.message}`)
      }
    }
    if (warns.length) {
      // eslint-disable-next-line no-console
      console.log('\nWARNINGS:')
      for (const w of warns) {
        // eslint-disable-next-line no-console
        console.log(`  [${w.tab}] ${w.message}`)
      }
    }
    // eslint-disable-next-line no-console
    console.log(`\nFull report: ${REPORT_PATH}`)
    // eslint-disable-next-line no-console
    console.log(`Screenshots: ${OUT}/`)

    expect(
      errors,
      `QA found ${errors.length} error(s). See ${REPORT_PATH}`,
    ).toEqual([])
  })
})
