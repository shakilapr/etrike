/**
 * Visual / contrast audit: open every workspace, screenshot, fail on
 * unreadable selection styles and obvious contrast regressions.
 */
import { test, expect, type Page } from '@playwright/test'
import path from 'node:path'
import fs from 'node:fs'

const OUT = path.join('test-results', 'visual-audit')

async function shot(page: Page, name: string) {
  fs.mkdirSync(OUT, { recursive: true })
  await page.screenshot({
    path: path.join(OUT, `${name}.png`),
    fullPage: true,
  })
}

/** Sample computed styles for common interactive states. */
async function sampleContrast(page: Page) {
  return page.evaluate(() => {
    function parseRgb(c: string): [number, number, number] | null {
      const m = c.match(/rgba?\((\d+),\s*(\d+),\s*(\d+)/)
      if (!m) return null
      return [Number(m[1]), Number(m[2]), Number(m[3])]
    }
    function lum([r, g, b]: [number, number, number]) {
      const s = [r, g, b].map((v) => {
        const x = v / 255
        return x <= 0.03928 ? x / 12.92 : ((x + 0.055) / 1.055) ** 2.4
      })
      return 0.2126 * s[0] + 0.7152 * s[1] + 0.0722 * s[2]
    }
    function contrast(fg: string, bg: string) {
      const a = parseRgb(fg)
      const b = parseRgb(bg)
      if (!a || !b) return null
      const L1 = lum(a)
      const L2 = lum(b)
      const hi = Math.max(L1, L2)
      const lo = Math.min(L1, L2)
      return (hi + 0.05) / (lo + 0.05)
    }
    function effectiveBg(el: Element): string {
      let n: Element | null = el
      while (n && n !== document.documentElement) {
        const bg = getComputedStyle(n as HTMLElement).backgroundColor
        if (bg && !bg.includes('rgba(0, 0, 0, 0)') && bg !== 'transparent') return bg
        n = n.parentElement
      }
      return getComputedStyle(document.body).backgroundColor
    }
    function sample(el: Element | null, label: string) {
      if (!el) return { label, missing: true }
      const cs = getComputedStyle(el as HTMLElement)
      const bg = effectiveBg(el)
      const fg = cs.color
      const ratio = contrast(fg, bg)
      return {
        label,
        bg,
        fg,
        ratio,
        fail: ratio != null && ratio < 4.0,
        text: (el.textContent || '').trim().slice(0, 40),
      }
    }

    const results: Array<Record<string, unknown>> = []
    results.push(sample(document.querySelector('.nav.active'), 'nav.active'))
    results.push(sample(document.querySelector('.chip'), 'chip'))
    results.push(sample(document.querySelector('.chip.ok'), 'chip.ok'))
    results.push(sample(document.querySelector('.chip.danger'), 'chip.danger'))
    results.push(sample(document.querySelector('.chip-v'), 'chip-v'))
    results.push(sample(document.querySelector('button:not(.secondary):not(.danger):not(.seg-btn):not(.nav):not(.gear-btn)'), 'btn.primary'))
    results.push(sample(document.querySelector('button.secondary'), 'btn.secondary'))
    results.push(sample(document.querySelector('button.danger'), 'btn.danger'))
    results.push(sample(document.querySelector('.seg-btn.active'), 'seg-btn.active'))
    results.push(sample(document.querySelector('.gear-btn.active'), 'gear-btn.active'))
    results.push(sample(document.querySelector('.fresh-live'), 'fresh-live'))
    results.push(sample(document.querySelector('.ws-header h1'), 'page-title'))
    results.push(sample(document.querySelector('main'), 'main'))
    results.push(sample(document.querySelector('.panel'), 'panel'))
    results.push(sample(document.querySelector('.card'), 'card'))
    results.push(sample(document.querySelector('.metric'), 'metric'))
    results.push(sample(document.querySelector('.log'), 'log'))
    results.push(sample(document.querySelector('.keycap.active'), 'keycap.active'))
    results.push(sample(document.querySelector('.btn-estop'), 'btn-estop'))

    // Selected table row if present
    const row = document.querySelector('.can-table tbody tr')
    if (row) {
      ;(row as HTMLElement).classList.add('selected')
      results.push(sample(row, 'table-row.selected'))
      results.push(sample(row.querySelector('td'), 'table-row.selected td'))
    }

    return results
  })
}

test.describe('Visual audit (control-ui light theme)', () => {
  test('capture workspaces and report contrast failures', async ({ page }) => {
    await page.goto('/')
    await expect(page.getByTestId('app')).toBeVisible()
    await expect(page.getByTestId('chip-stream')).not.toHaveText(/CONNECTING/i, {
      timeout: 20_000,
    })

    const workspaces = [
      'overview',
      'network',
      'live',
      'control',
      'preview',
      'bench',
      'dictionary',
      'diagnostics',
      'settings',
    ] as const

    const allFails: Array<Record<string, unknown>> = []

    for (const id of workspaces) {
      await page.getByTestId(`nav-${id}`).click()
      await expect(page.getByTestId(`workspace-${id === 'preview' ? 'preview' : id}`)).toBeVisible({
        timeout: 10_000,
      })
      await page.waitForTimeout(250)
      await shot(page, id)

      const samples = await sampleContrast(page)
      const fails = samples.filter((s) => s.fail)
      for (const f of fails) {
        allFails.push({ workspace: id, ...f })
      }
      // Soft assert per page — collect then fail once
      console.log(`[${id}] samples:`, JSON.stringify(samples, null, 0))
    }

    // Drive: arm + keycaps
    await page.getByTestId('nav-preview').click()
    await page.getByTestId('preview-canvas-wrap').click()
    await page.getByTestId('btn-drive-arm').click()
    await expect(page.getByTestId('btn-drive-disarm')).toBeVisible({ timeout: 15_000 })
    await page.waitForTimeout(300)
    await shot(page, 'drive-armed')

    // Control direct section
    await page.getByTestId('nav-control').click()
    await shot(page, 'control-direct')

    // Live with inject for row selection colors
    await page.getByTestId('nav-control').click()
    await page.getByTestId('btn-inject-drive').click()
    await page.waitForTimeout(800)
    await page.getByTestId('nav-live').click()
    await page.waitForTimeout(500)
    const firstRow = page.locator('.can-table tbody tr').first()
    if (await firstRow.count()) {
      await firstRow.click()
      await shot(page, 'live-selected-row')
    }

    fs.mkdirSync(OUT, { recursive: true })
    fs.writeFileSync(
      path.join(OUT, 'contrast-report.json'),
      JSON.stringify({ fails: allFails, at: new Date().toISOString() }, null, 2),
    )

    if (allFails.length) {
      console.error('CONTRAST FAILURES', JSON.stringify(allFails, null, 2))
    }
    expect(
      allFails,
      `Low-contrast elements (ratio < 3):\n${JSON.stringify(allFails, null, 2)}`,
    ).toEqual([])
  })
})
