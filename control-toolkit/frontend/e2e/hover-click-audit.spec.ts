/**
 * Fast geometry audit: hover/active must not grow controls or collide neighbors.
 * Uses real pointer hover on a curated set of controls per workspace.
 */
import { test, expect, type Page, type Locator } from '@playwright/test'
import path from 'node:path'
import fs from 'node:fs'

const OUT = path.join('test-results', 'hover-click-audit')
const GROW = 2.0
const OVERLAP_AREA = 16

type Issue = {
  workspace: string
  kind: 'hover-grow' | 'click-grow' | 'hover-overlap'
  target: string
  detail: string
  other?: string
}

async function measure(loc: Locator) {
  return loc.evaluate((el) => {
    const r = el.getBoundingClientRect()
    const h = el as HTMLElement
    return {
      x: r.x,
      y: r.y,
      w: r.width,
      h: r.height,
      ow: h.offsetWidth,
      oh: h.offsetHeight,
      transform: getComputedStyle(h).transform,
    }
  })
}

async function auditLocator(
  page: Page,
  workspace: string,
  loc: Locator,
  label: string,
  issues: Issue[],
) {
  if (!(await loc.count())) return
  const target = loc.first()
  try {
    await target.scrollIntoViewIfNeeded({ timeout: 1500 })
  } catch {
    return
  }
  if (!(await target.isVisible().catch(() => false))) return

  const before = await measure(target)
  await target.hover({ force: true, timeout: 2000 }).catch(() => undefined)
  await page.waitForTimeout(40)
  const hover = await measure(target)

  const dOw = hover.ow - before.ow
  const dOh = hover.oh - before.oh
  const dW = hover.w - before.w
  const dH = hover.h - before.h
  if (dOw > GROW || dOh > GROW || dW > GROW + 0.5 || dH > GROW + 0.5) {
    issues.push({
      workspace,
      kind: 'hover-grow',
      target: label,
      detail: `hover layout(+${dOw.toFixed(1)},+${dOh.toFixed(1)}) visual(+${dW.toFixed(1)},+${dH.toFixed(1)}) transform=${hover.transform}`,
    })
  }

  // Overlaps with other buttons while hovered
  const overlaps = await page.evaluate(
    ({ hx, hy, hw, hh, label: selfLabel }) => {
      const hits: Array<{ other: string; area: number }> = []
      const nodes = Array.from(
        document.querySelectorAll(
          'button, a[href], [role="button"], [role="tab"], .seg-btn, .gear-btn, .bit-cell, .nav',
        ),
      ) as HTMLElement[]
      for (const el of nodes) {
        const r = el.getBoundingClientRect()
        if (r.width < 2 || r.height < 2) continue
        const tid = el.getAttribute('data-testid') || ''
        const text = (el.innerText || '').replace(/\s+/g, ' ').trim().slice(0, 24)
        const other = `${el.tagName.toLowerCase()}${tid ? `[${tid}]` : ''} "${text}"`
        if (other === selfLabel) continue
        // skip if same element roughly same box as hover target
        if (Math.abs(r.x - hx) < 0.5 && Math.abs(r.y - hy) < 0.5 && Math.abs(r.width - hw) < 0.5)
          continue
        const x1 = Math.max(hx, r.x)
        const y1 = Math.max(hy, r.y)
        const x2 = Math.min(hx + hw, r.x + r.width)
        const y2 = Math.min(hy + hh, r.y + r.height)
        const w = x2 - x1
        const h = y2 - y1
        if (w < 2 || h < 2) continue
        const area = w * h
        if (area < 16) continue
        // containment (parent chrome) skip
        const contained =
          hx >= r.x - 1 && hy >= r.y - 1 && hx + hw <= r.x + r.width + 1 && hy + hh <= r.y + r.height + 1
        const contains =
          r.x >= hx - 1 && r.y >= hy - 1 && r.x + r.width <= hx + hw + 1 && r.y + r.height <= hy + hh + 1
        if (contained || contains) continue
        hits.push({ other, area: Math.round(area) })
      }
      return hits.slice(0, 8)
    },
    { hx: hover.x, hy: hover.y, hw: hover.w, hh: hover.h, label },
  )
  for (const o of overlaps) {
    issues.push({
      workspace,
      kind: 'hover-overlap',
      target: label,
      other: o.other,
      detail: `overlap ${o.area}px² while hovered`,
    })
  }

  // mousedown / active grow
  await page.mouse.down()
  await page.waitForTimeout(30)
  const down = await measure(target)
  await page.mouse.up()
  const dOw2 = down.ow - hover.ow
  const dOh2 = down.oh - hover.oh
  const dW2 = down.w - hover.w
  const dH2 = down.h - hover.h
  if (dOw2 > GROW || dOh2 > GROW || dW2 > GROW + 0.5 || dH2 > GROW + 0.5) {
    issues.push({
      workspace,
      kind: 'click-grow',
      target: label,
      detail: `mousedown layout(+${dOw2.toFixed(1)},+${dOh2.toFixed(1)}) visual(+${dW2.toFixed(1)},+${dH2.toFixed(1)})`,
    })
  }

  await page.mouse.move(4, 4)
  await page.waitForTimeout(15)
}

test.describe('Hover / click geometry stability', () => {
  test('no expanding controls or hover collisions across workspaces', async ({ page }) => {
    test.setTimeout(120_000)
    await page.setViewportSize({ width: 1440, height: 900 })
    await page.goto('/')
    await expect(page.getByTestId('app')).toBeVisible()
    await expect(page.getByTestId('chip-stream')).not.toHaveText(/CONNECTING/i, {
      timeout: 20_000,
    })

    const issues: Issue[] = []
    fs.mkdirSync(OUT, { recursive: true })

    const workspaces = [
      'overview',
      'live',
      'control',
      'preview',
      'dictionary',
      'diagnostics',
      'settings',
    ] as const

    for (const id of workspaces) {
      await page.getByTestId(`nav-${id}`).click()
      const ws = id === 'preview' ? 'preview' : id
      await expect(page.getByTestId(`workspace-${ws}`)).toBeVisible({ timeout: 10_000 })
      await page.waitForTimeout(250)

      // Curated high-risk controls per page
      const probes: Array<{ loc: Locator; label: string }> = [
        { loc: page.getByTestId('btn-header-estop'), label: 'btn-header-estop' },
        { loc: page.getByTestId(`nav-${id}`), label: `nav-${id}` },
        { loc: page.locator('button.nav').nth(0), label: 'nav-first' },
        { loc: page.locator('button.secondary').first(), label: 'button.secondary' },
        { loc: page.locator('button.danger').first(), label: 'button.danger' },
        { loc: page.locator('button.seg-btn').first(), label: 'seg-btn' },
        { loc: page.locator('button.seg-btn.active').first(), label: 'seg-btn.active' },
        { loc: page.locator('.bus-tabs button').first(), label: 'bus-tabs' },
        { loc: page.locator('button.gear-btn').first(), label: 'gear-btn' },
        { loc: page.locator('button.vehicle-open-drive').first(), label: 'vehicle-open-drive' },
        { loc: page.getByTestId('btn-drive-arm'), label: 'btn-drive-arm' },
        { loc: page.getByTestId('btn-drive-estop'), label: 'btn-drive-estop' },
        { loc: page.getByTestId('btn-stop-all'), label: 'btn-stop-all' },
        { loc: page.getByTestId('btn-enable-tx'), label: 'btn-enable-tx' },
        { loc: page.getByTestId('dict-refresh'), label: 'dict-refresh' },
        { loc: page.getByTestId('filter-bus-high'), label: 'filter-bus-high' },
        { loc: page.getByTestId('live-mode-chrono'), label: 'live-mode-chrono' },
      ]

      // Also sample a few plain primary buttons
      const primaryCount = await page
        .locator(
          'button:not(.nav):not(.seg-btn):not(.gear-btn):not(.bit-cell):not(.bit-legend-card):not(.secondary):not(.danger):not(.btn-estop):not(.vehicle-open-drive)',
        )
        .count()
      if (primaryCount > 0) {
        probes.push({
          loc: page
            .locator(
              'button:not(.nav):not(.seg-btn):not(.gear-btn):not(.bit-cell):not(.bit-legend-card):not(.secondary):not(.danger):not(.btn-estop):not(.vehicle-open-drive)',
            )
            .first(),
          label: 'button.primary-ish',
        })
      }

      for (const p of probes) {
        await auditLocator(page, id, p.loc, p.label, issues)
      }

      await page.screenshot({ path: path.join(OUT, `${id}.png`) })
    }

    // Dedupe
    const seen = new Set<string>()
    const uniq = issues.filter((i) => {
      const k = `${i.kind}|${i.workspace}|${i.target}|${i.other || ''}|${i.detail}`
      if (seen.has(k)) return false
      seen.add(k)
      return true
    })

    const report = {
      at: new Date().toISOString(),
      summary: {
        total: uniq.length,
        hoverGrow: uniq.filter((i) => i.kind === 'hover-grow').length,
        clickGrow: uniq.filter((i) => i.kind === 'click-grow').length,
        hoverOverlap: uniq.filter((i) => i.kind === 'hover-overlap').length,
      },
      issues: uniq,
    }
    fs.writeFileSync(path.join(OUT, 'report.json'), JSON.stringify(report, null, 2))
    console.log('=== HOVER/CLICK AUDIT ===')
    console.log(JSON.stringify(report.summary, null, 2))
    for (const i of uniq) {
      console.log(`[${i.kind}] ${i.workspace} :: ${i.target}${i.other ? ' × ' + i.other : ''} — ${i.detail}`)
    }

    expect(
      uniq,
      uniq.map((i) => `[${i.kind}] ${i.workspace} ${i.target}: ${i.detail}`).join('\n'),
    ).toEqual([])
  })
})
