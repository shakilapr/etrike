import { chromium } from 'playwright';

(async () => {
  // Launch in non-headless mode so the user can watch it!
  const browser = await chromium.launch({ headless: false, slowMo: 500 });
  const context = await browser.newContext({
    viewport: { width: 1280, height: 720 }
  });
  const page = await context.newPage();
  
  console.log("Opening debug browser...");
  await page.goto('http://127.0.0.1:5174/');
  
  console.log("Waiting for the app shell to load...");
  await page.waitForFunction(() => document.querySelector('.app-shell') !== null, { timeout: 10000 });
  
  console.log("Demonstrating the mode dropdown fix...");
  const modeSelect = page.locator('.tb-mode-select');
  await modeSelect.click();
  await page.waitForTimeout(1000);
  await modeSelect.selectOption('full-sim');
  
  console.log("Demonstrating the sidebar toggle fix...");
  const sidebarToggle = page.locator('.trike-sidebar-toggle');
  await sidebarToggle.click();
  await page.waitForTimeout(1000);
  await sidebarToggle.click();
  await page.waitForTimeout(1000);
  
  console.log("Demonstrating the flex layout fixes on tabs...");
  const tabs = page.locator('.tabs button');
  const tabCount = await tabs.count();
  for (let i = 0; i < tabCount; i++) {
    await tabs.nth(i).click();
    await page.waitForTimeout(500);
  }
  
  console.log("Debug browser demonstration complete. Closing in 5 seconds...");
  await page.waitForTimeout(5000);
  await browser.close();
})();
