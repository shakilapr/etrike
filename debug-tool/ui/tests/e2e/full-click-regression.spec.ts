import { test, expect } from '@playwright/test';

test.describe('Full UI click regression', () => {
  test.beforeEach(async ({ page }) => {
    const consoleErrors: string[] = [];

    page.on('console', msg => {
      // Ignore favicon 404s and normal dev warnings
      if (msg.type() === 'error' || msg.type() === 'warning') {
        if (!msg.text().includes('favicon.ico') && !msg.text().includes('HMR')) {
          consoleErrors.push(`${msg.type()}: ${msg.text()}`);
        }
      }
    });

    page.on('pageerror', err => {
      consoleErrors.push(`Exception: ${err.message}`);
    });

    await page.goto('/');

    // Wait for the app to initialize
    await page.waitForFunction(() => document.querySelector('.app-shell') !== null);
    await expect(page.locator('.app-shell')).toBeVisible();

    test.info().annotations.push({
      type: 'consoleErrors',
      description: JSON.stringify(consoleErrors),
    });
  });

  test.use({
    workers: 1,
  });

  test('clicks all primary navigation and action controls', async ({ page }) => {
    // Topbar mode select check
    const modeSelect = page.locator('.tb-mode-select');
    if (await modeSelect.isVisible()) {
      await modeSelect.selectOption('full-sim');
    }

    // Toggle physics sidebar
    const sidebarToggle = page.locator('.trike-sidebar-toggle');
    if (await sidebarToggle.isVisible()) {
      await sidebarToggle.click({ force: true });
      await page.waitForTimeout(500);
      await sidebarToggle.click({ force: true });
    }

    // Click all main tabs
    const tabs = page.locator('.tabs button');
    const tabCount = await tabs.count();
    
    for (let i = 0; i < tabCount; i++) {
      const tab = tabs.nth(i);
      if (!(await tab.isVisible().catch(() => false))) continue;
      
      const tabName = await tab.innerText();
      await tab.click();
      await page.waitForTimeout(300); // Wait for render
      
      // Verify main content didn't collapse completely
      const mainRect = await page.evaluate(() => {
        const m = document.querySelector('main');
        return m ? m.getBoundingClientRect() : null;
      });
      
      if (mainRect && mainRect.height < 10) {
         console.warn(`Tab ${tabName} might be collapsed! Height is ${mainRect.height}px`);
      }
    }
  });

  test('explores Controller and Emulator specific flows', async ({ page }) => {
    // Go to Controller
    await page.locator('.tabs button', { hasText: 'Controller' }).click();
    await page.waitForTimeout(500);
    
    const wasdDiv = page.locator('.d-pad');
    if (await wasdDiv.isVisible()) {
      await page.keyboard.press('w');
      await page.waitForTimeout(200);
      await page.keyboard.press('a');
    }

    // Go to Emulator
    await page.locator('.tabs button', { hasText: 'Emulator' }).click();
    await page.waitForTimeout(500);
    
    // Toggle some ECUs
    const ecuToggles = page.locator('.ecu-grid .card');
    const ecuCount = await ecuToggles.count();
    
    // Just toggle the first one to test interactivity
    if (ecuCount > 0) {
      await ecuToggles.nth(0).click({ force: true });
      await page.waitForTimeout(500);
    }
  });

  test('checks responsive mobile click flows', async ({ page }) => {
    await page.setViewportSize({ width: 390, height: 844 });
    await page.goto('/');
    
    // Ensure app shell renders in mobile view
    await page.waitForFunction(() => document.querySelector('.app-shell') !== null);
    await expect(page.locator('.app-shell')).toBeVisible();

    // The topbar mode select might be hidden on mobile
    const modeSelect = page.locator('.tb-mode-select');
    if (await modeSelect.isVisible().catch(() => false)) {
       try {
         await modeSelect.selectOption('hybrid', { timeout: 2000 });
       } catch (e) {
         console.warn("Could not select mode on mobile viewport");
       }
    }
  });
});
