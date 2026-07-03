import { chromium } from 'playwright';

const b = await chromium.launch();
const p = await b.newPage();
const issues = [];

p.on('console', msg => { if (msg.type() === 'error') issues.push('CONSOLE: ' + msg.text()); });
p.on('pageerror', err => issues.push('PAGE: ' + err.message));

await p.setViewportSize({ width: 1280, height: 800 });
await p.goto('http://127.0.0.1:5173/', { waitUntil: 'networkidle', timeout: 20000 });
await p.waitForTimeout(3000);

// ── Check for hidden/duplicate elements ──
const dupes = await p.evaluate(() => {
  const problems = [];
  // Old v2 classes that should be hidden
  const oldClasses = ['.brand-compact', '.health-dots', '.bulb-strip', '.vstate-strip', '.cmd-strip', '.telemetry-strip', '.health-strip-v2', '.estop-indicator'];
  for (const cls of oldClasses) {
    const el = document.querySelector(cls);
    if (el && getComputedStyle(el).display !== 'none') {
      problems.push('OLD ELEMENT VISIBLE: ' + cls);
    }
  }
  // Health items — check they have proper gap visually
  const healthEls = document.querySelectorAll('.tbh');
  if (healthEls.length > 1) {
    const r1 = healthEls[0].getBoundingClientRect();
    const r2 = healthEls[1].getBoundingClientRect();
    if (r2.left - r1.right < 4) problems.push('Health items too close: gap=' + (r2.left - r1.right) + 'px');
  }
  // Check topbar rows don't wrap unexpectedly
  const rows = document.querySelectorAll('.tb-row');
  rows.forEach((r, i) => {
    if (r.scrollHeight > r.clientHeight + 2) problems.push('Row ' + (i+1) + ' wraps: scrollH=' + r.scrollHeight + ' clientH=' + r.clientHeight);
  });
  // Check mode pill background is visible (not transparent)
  const mode = document.querySelector('.tvs-mode');
  if (mode) {
    const bg = getComputedStyle(mode).backgroundColor;
    if (bg === 'rgba(0, 0, 0, 0)' || bg === 'transparent') problems.push('Mode pill has no background');
  }
  return problems;
});
dupes.forEach(d => issues.push('LAYOUT: ' + d));

// ── Check each tab for empty/broken content ──
for (const tab of ['Dashboard','CAN Monitor','CAN Dictionary','Injector','Controller','Unit Test','Pipeline','Statistics','Terminal','Simulator']) {
  try {
    await p.click('button:has-text("' + tab + '")');
    await p.waitForTimeout(400);
    const main = await p.$('main');
    if (!main) { issues.push('TAB: ' + tab + ' has no <main>'); continue; }
    const text = (await main.textContent()).replace(/\s+/g, ' ').trim();
    if (text.length < 20) issues.push('TAB: ' + tab + ' appears empty (' + text.length + ' chars)');
    if (text.includes('undefined') || text.includes('null') || text.includes('NaN')) issues.push('TAB: ' + tab + ' shows undefined/null/NaN');
  } catch(e) {
    issues.push('TAB: ' + tab + ' click failed: ' + e.message);
  }
}

// ── Check Terminal severity colors ──
await p.click('button:has-text("Terminal")');
await p.waitForTimeout(300);
const termColors = await p.evaluate(() => {
  return Array.from(document.querySelectorAll('.terminal-line')).map(l => {
    const cls = l.classList.contains('info') ? 'info' : l.classList.contains('warn') ? 'warn' : 'error';
    const color = getComputedStyle(l).borderLeftColor;
    return { cls, color };
  });
});
if (termColors.length > 0) {
  const infoOk  = termColors.filter(t => t.cls === 'info').every(t => t.color === 'rgb(76, 175, 130)');
  const warnOk  = termColors.filter(t => t.cls === 'warn').every(t => t.color === 'rgb(224, 159, 62)');
  const errorOk = termColors.filter(t => t.cls === 'error').every(t => t.color === 'rgb(224, 85, 106)');
  if (!infoOk)  issues.push('TERMINAL: info color wrong');
  if (!warnOk)  issues.push('TERMINAL: warn color wrong');
  if (!errorOk) issues.push('TERMINAL: error color wrong');
}

// ── Check Simulator buttons ──
await p.click('button:has-text("Simulator")');
await p.waitForTimeout(300);
const simBtns = await p.evaluate(() => {
  const master = document.querySelector('.sim-master');
  const cards = document.querySelectorAll('.sim-card');
  const problems = [];
  if (!master) problems.push('Sim master button missing');
  if (cards.length === 0) problems.push('Sim has 0 cards');
  // Check each card has checkbox
  cards.forEach(c => {
    if (!c.querySelector('input[type="checkbox"]')) problems.push('Sim card missing checkbox: ' + c.textContent?.trim().substring(0,20));
  });
  return problems;
});
simBtns.forEach(s => issues.push('SIM: ' + s));

// ── Check Controller gear buttons ──
await p.click('button:has-text("Controller")');
await p.waitForTimeout(300);
const gearBtns = await p.evaluate(() => {
  const btns = document.querySelectorAll('.gear-btn');
  return btns.length === 4 ? null : 'Controller has ' + btns.length + ' gear buttons (expected 4)';
});
if (gearBtns) issues.push('CONTROLLER: ' + gearBtns);

// ── Final report ──
if (issues.length === 0) {
  console.log('ALL CLEAN — no issues found ✅');
} else {
  console.log('ISSUES FOUND (' + issues.length + '):');
  issues.forEach(i => console.log('  ❌ ' + i));
}

await b.close();
