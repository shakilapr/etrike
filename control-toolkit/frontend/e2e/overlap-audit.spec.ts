/**
 * Geometry audit: detect overlapping buttons/controls and clipped interactive UI.
 */
import { test, expect, type Page } from '@playwright/test'
import path from 'node:path'
import fs from 'node:fs'

const OUT = path.join('test-results', 'overlap-audit')

type Box = { x: number; y: number; w: number; h: number }
type Hit = {
  a: string
  b: string
  area: number
  aBox: Box
  bBox: Box
  workspace: string
  kind: 'overlap' | 'zero-size' | 'offscreen' | 'clipped'
  detail?: string
}

function labelOf(el: { tag: string; testId: string; cls: string; text: string; role: string }) {
  const tid = el.testId ? `[${el.testId}]` : ''
  const txt = el.text ? `"${el.text.slice(0, 28)}"` : ''
  const cls = el.cls ? `.${el.cls.split(/\s+/).filter(Boolean).slice(0, 3).join('.')}` : ''
  return `${el.tag}${tid}${cls} ${txt}`.trim()
}

async function collectIssues(page: Page, workspace: string): Promise<Hit[]> {
  return page.evaluate((ws) => {
    const hits: Array<{
      a: string
      b: string
      area: number
      aBox: { x: number; y: number; w: number; h: number }
      bBox: { x: number; y: number; w: number; h: number }
      workspace: string
      kind: 'overlap' | 'zero-size' | 'offscreen' | 'clipped'
      detail?: string
    }> = []

    const selector = [
      'button',
      'a[href]',
      'input',
      'select',
      'textarea',
      '[role="button"]',
      '[role="tab"]',
      '.nav',
      '.seg-btn',
      '.gear-btn',
      '.bit-cell',
      '.keycap',
    ].join(',')

    const nodes = Array.from(document.querySelectorAll(selector)) as HTMLElement[]
    const items: Array<{
      el: HTMLElement
      label: string
      box: { x: number; y: number; w: number; h: number }
      z: number
    }> = []

    function isVisible(el: HTMLElement) {
      const cs = getComputedStyle(el)
      if (cs.display === 'none' || cs.visibility === 'hidden' || cs.opacity === '0') return false
      if (cs.pointerEvents === 'none') return false
      const r = el.getBoundingClientRect()
      if (r.width < 1 || r.height < 1) return false
      return true
    }

    function label(el: HTMLElement) {
      const tid = el.getAttribute('data-testid') || ''
      const cls = (el.className && typeof el.className === 'string' ? el.className : '')
        .split(/\s+/)
        .filter(Boolean)
        .slice(0, 4)
        .join('.')
      const text = (el.innerText || el.getAttribute('aria-label') || el.getAttribute('title') || '')
        .replace(/\s+/g, ' ')
        .trim()
        .slice(0, 32)
      return `${el.tagName.toLowerCase()}${tid ? `[${tid}]` : ''}${cls ? `.${cls}` : ''} ${text ? `"${text}"` : ''}`.trim()
    }

    function areaIntersect(
      a: { x: number; y: number; w: number; h: number },
      b: { x: number; y: number; w: number; h: number },
    ) {
      const x1 = Math.max(a.x, b.x)
      const y1 = Math.max(a.y, b.y)
      const x2 = Math.min(a.x + a.w, b.x + b.w)
      const y2 = Math.min(a.y + a.h, b.y + b.h)
      const w = x2 - x1
      const h = y2 - y1
      if (w <= 0 || h <= 0) return 0
      return w * h
    }

    const vw = window.innerWidth
    const vh = window.innerHeight

    for (const el of nodes) {
      const r = el.getBoundingClientRect()
      const box = { x: r.x, y: r.y, w: r.width, h: r.height }
      const lab = label(el)

      // zero-size interactive (often bad layout)
      if ((el.tagName === 'BUTTON' || el.getAttribute('role') === 'button') && (r.width < 2 || r.height < 2)) {
        const cs = getComputedStyle(el)
        if (cs.display !== 'none' && cs.visibility !== 'hidden') {
          hits.push({
            a: lab,
            b: '',
            area: 0,
            aBox: box,
            bBox: box,
            workspace: ws,
            kind: 'zero-size',
            detail: `${r.width.toFixed(1)}x${r.height.toFixed(1)}`,
          })
        }
      }

      if (!isVisible(el)) continue

      // Fully left/right of viewport (not merely below fold — scroll is OK)
      if (r.right < 0 || r.left > vw) {
        hits.push({
          a: lab,
          b: '',
          area: 0,
          aBox: box,
          bBox: box,
          workspace: ws,
          kind: 'offscreen',
          detail: `viewport ${vw}x${vh}`,
        })
        continue
      }
      // Above viewport top with no way to scroll into view is rare; skip below-fold
      if (r.bottom < 0) {
        hits.push({
          a: lab,
          b: '',
          area: 0,
          aBox: box,
          bBox: box,
          workspace: ws,
          kind: 'offscreen',
          detail: `above-viewport ${vw}x${vh}`,
        })
        continue
      }

      const z = Number(getComputedStyle(el).zIndex) || 0
      items.push({ el, label: lab, box, z })
    }

    // pairwise overlap among visible interactives (ignore nested pairs)
    for (let i = 0; i < items.length; i++) {
      for (let j = i + 1; j < items.length; j++) {
        const A = items[i]
        const B = items[j]
        if (A.el.contains(B.el) || B.el.contains(A.el)) continue
        // same parent segment groups often share edge borders — require real area
        const area = areaIntersect(A.box, B.box)
        // Ignore 1px border-sharing (area <= width or height of 1px line)
        if (area < 8) continue
        // Ignore nearly-touching edges: if either dimension of intersection is < 2px
        const x1 = Math.max(A.box.x, B.box.x)
        const y1 = Math.max(A.box.y, B.box.y)
        const x2 = Math.min(A.box.x + A.box.w, B.box.x + B.box.w)
        const y2 = Math.min(A.box.y + A.box.h, B.box.y + B.box.h)
        if (x2 - x1 < 2 || y2 - y1 < 2) continue

        hits.push({
          a: A.label,
          b: B.label,
          area: Math.round(area),
          aBox: {
            x: Math.round(A.box.x),
            y: Math.round(A.box.y),
            w: Math.round(A.box.w),
            h: Math.round(A.box.h),
          },
          bBox: {
            x: Math.round(B.box.x),
            y: Math.round(B.box.y),
            w: Math.round(B.box.w),
            h: Math.round(B.box.h),
          },
          workspace: ws,
          kind: 'overlap',
        })
      }
    }

    // Clipped overflow: only report horizontal clip of buttons by overflow:hidden
    // ancestors (vertical scroll containers are intentional).
    for (const it of items) {
      if (it.el.tagName !== 'BUTTON' && it.el.getAttribute('role') !== 'button') continue
      let p: HTMLElement | null = it.el.parentElement
      while (p && p !== document.body && p !== document.documentElement) {
        const cs = getComputedStyle(p)
        const ox = cs.overflowX
        const oy = cs.overflowY
        const clipsX = ox === 'hidden' || ox === 'clip'
        const scrollsY = oy === 'auto' || oy === 'scroll' || oy === 'hidden'
        if (!clipsX && !scrollsY && cs.overflow !== 'hidden') {
          p = p.parentElement
          continue
        }
        const pr = p.getBoundingClientRect()
        const er = it.el.getBoundingClientRect()
        // Horizontal clip only (ignore vertical scroll-away)
        if (clipsX || cs.overflow === 'hidden') {
          const clipLeft = Math.max(0, pr.left - er.left)
          const clipRight = Math.max(0, er.right - pr.right)
          if (clipLeft > 4 || clipRight > 4) {
            hits.push({
              a: it.label,
              b: `parent.${(p.className || '').toString().split(/\s+/).slice(0, 2).join('.')}`,
              area: Math.round((clipLeft + clipRight) * Math.max(er.height, 1)),
              aBox: {
                x: Math.round(er.x),
                y: Math.round(er.y),
                w: Math.round(er.width),
                h: Math.round(er.height),
              },
              bBox: {
                x: Math.round(pr.x),
                y: Math.round(pr.y),
                w: Math.round(pr.width),
                h: Math.round(pr.height),
              },
              workspace: ws,
              kind: 'clipped',
              detail: `clipX L=${clipLeft.toFixed(0)} R=${clipRight.toFixed(0)}`,
            })
          }
        }
        // stop at first clipping/scroll ancestor
        break
      }
    }

    return hits
  }, workspace)
}

test.describe('Overlap / geometry audit', () => {
  test('no overlapping interactive controls across workspaces', async ({ page }) => {
    await page.setViewportSize({ width: 1440, height: 900 })
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
      'logs',
      'settings',
    ] as const

    const all: Hit[] = []
    fs.mkdirSync(OUT, { recursive: true })

    for (const id of workspaces) {
      const nav = page.getByTestId(`nav-${id}`)
      if (!(await nav.count())) continue
      await nav.click()
      const wsId = id === 'preview' ? 'preview' : id
      await expect(page.getByTestId(`workspace-${wsId}`)).toBeVisible({ timeout: 10_000 })
      await page.waitForTimeout(300)

      // Extra states that expose more controls
      if (id === 'control') {
        const low = page.getByTestId('control-method-low')
        if (await low.count()) {
          await low.click()
          await page.waitForTimeout(200)
        }
      }
      if (id === 'preview') {
        const arm = page.getByTestId('btn-drive-arm')
        if (await arm.count()) {
          await arm.click().catch(() => {})
          await page.waitForTimeout(400)
        }
      }

      const hits = await collectIssues(page, id)
      all.push(...hits)

      await page.screenshot({
        path: path.join(OUT, `${id}.png`),
        fullPage: false,
      })
    }

    // Also audit at a tighter width (common laptop) for wrap/overlap
    await page.setViewportSize({ width: 1280, height: 800 })
    for (const id of ['overview', 'control', 'preview', 'dictionary', 'live'] as const) {
      await page.getByTestId(`nav-${id}`).click()
      await page.waitForTimeout(250)
      const hits = await collectIssues(page, `${id}@1280`)
      all.push(...hits)
      await page.screenshot({
        path: path.join(OUT, `${id}-1280.png`),
        fullPage: false,
      })
    }

    // Narrow width where topbar often collapses poorly
    await page.setViewportSize({ width: 1024, height: 768 })
    for (const id of ['overview', 'control', 'preview'] as const) {
      await page.getByTestId(`nav-${id}`).click()
      await page.waitForTimeout(250)
      const hits = await collectIssues(page, `${id}@1024`)
      all.push(...hits)
      await page.screenshot({
        path: path.join(OUT, `${id}-1024.png`),
        fullPage: false,
      })
    }

    const overlaps = all.filter((h) => h.kind === 'overlap')
    const zero = all.filter((h) => h.kind === 'zero-size')
    const off = all.filter((h) => h.kind === 'offscreen')
    const clipped = all.filter((h) => h.kind === 'clipped')

    const report = {
      at: new Date().toISOString(),
      summary: {
        total: all.length,
        overlaps: overlaps.length,
        zeroSize: zero.length,
        offscreen: off.length,
        clipped: clipped.length,
      },
      overlaps,
      zeroSize: zero,
      offscreen: off,
      clipped,
    }
    fs.writeFileSync(path.join(OUT, 'report.json'), JSON.stringify(report, null, 2))
    console.log('=== OVERLAP AUDIT ===')
    console.log(JSON.stringify(report.summary, null, 2))
    if (overlaps.length) {
      console.log(
        'OVERLAPS:\n',
        overlaps
          .slice(0, 40)
          .map((h) => `  [${h.workspace}] ${h.a}  ×  ${h.b}  area=${h.area}`)
          .join('\n'),
      )
    }
    if (clipped.length) {
      console.log(
        'CLIPPED:\n',
        clipped
          .slice(0, 20)
          .map((h) => `  [${h.workspace}] ${h.a} in ${h.b} ${h.detail || ''}`)
          .join('\n'),
      )
    }
    if (zero.length) {
      console.log(
        'ZERO-SIZE:\n',
        zero
          .slice(0, 20)
          .map((h) => `  [${h.workspace}] ${h.a} ${h.detail || ''}`)
          .join('\n'),
      )
    }

    // Fail hard on real overlaps; soft-note clipped/zero for report
    expect(
      overlaps,
      `Found ${overlaps.length} overlapping interactive controls:\n${overlaps
        .slice(0, 30)
        .map((h) => `[${h.workspace}] ${h.a} × ${h.b} area=${h.area}`)
        .join('\n')}`,
    ).toEqual([])
  })
})
