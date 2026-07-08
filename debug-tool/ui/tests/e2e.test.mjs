import { chromium } from 'playwright';

(async () => {
  console.log("Launching browser for complete QA test...");
  const browser = await chromium.launch({ headless: false, slowMo: 1000 });
  const page = await browser.newPage();
  
  const bugs = [];

  page.on('console', msg => {
    if (msg.type() === 'error' || msg.type() === 'warning') {
      // Ignore favicon 404s
      if (msg.text().includes('favicon.ico')) return;
      bugs.push(`Console ${msg.type()}: ${msg.text()}`);
    }
  });

  page.on('pageerror', error => {
    bugs.push(`Page Error: ${error.message}`);
  });

  try {
    const url = 'http://127.0.0.1:5174/';
    console.log(`Testing UI at ${url}...`);
    
    await page.goto(url, { timeout: 10000 });
    
    // Use DOM query instead of strict visibility wait
    await page.waitForFunction(() => document.querySelector('.app-shell') !== null, { timeout: 10000 });
    await page.waitForTimeout(1000);
    
    // 1. Check all tabs
    console.log("Checking all navigation tabs...");
    const tabs = await page.locator('.tabs button').elementHandles();
    for (const tab of tabs) {
      const tabName = await tab.innerText();
      console.log(`Clicking tab: ${tabName}`);
      await tab.click({ force: true });
      await page.waitForTimeout(500); // allow render
      
      // Check for any obvious layout collapses by checking main height
      const mainRect = await page.evaluate(() => {
        const m = document.querySelector('main');
        return m ? m.getBoundingClientRect() : null;
      });
      if (mainRect && mainRect.height < 10) {
        bugs.push(`BUG: Tab "${tabName}" rendered with height ${mainRect.height}px (Layout collapse)`);
      }
    }

    // 2. Controller Tab QA
    console.log("Testing Controller tab...");
    const controllerTab = page.locator('.tabs button', { hasText: 'Controller' });
    await controllerTab.click({ force: true });
    await page.waitForTimeout(1000);
    // Try sending some input
    const wasdDiv = page.locator('.d-pad');
    if (await wasdDiv.isVisible()) {
      await page.keyboard.press('w');
      await page.waitForTimeout(200);
      await page.keyboard.press('a');
      console.log("Pressed WASD keys");
    }

    // 3. Emulator Tab QA
    console.log("Testing Emulator tab...");
    const emulatorTab = page.locator('.tabs button', { hasText: 'Emulator' });
    await emulatorTab.click({ force: true });
    await page.waitForTimeout(1000);
    
    // Toggle an ECU
    const ecuToggles = await page.locator('.ecu-grid .card').elementHandles();
    if (ecuToggles.length > 0) {
      console.log(`Found ${ecuToggles.length} ECUs to interact with.`);
      await ecuToggles[0].click({ force: true });
      await page.waitForTimeout(500);
    }

    // 4. Physics View Sidebar 
    console.log("Testing Physics View Sidebar...");
    const sidebarToggle = page.locator('.trike-sidebar-toggle');
    await sidebarToggle.click({ force: true });
    await page.waitForTimeout(1500);
    
  } catch (error) {
    console.log("EXECUTION ERROR: " + error.stack);
    bugs.push(`Test execution error: ${error.message}`);
  } finally {
    console.log("Closing browser...");
    await browser.close();
  }

  console.log("---- BUG REPORT ----");
  if (bugs.length > 0) {
    bugs.forEach(b => console.log("- " + b));
  } else {
    console.log("No new UI bugs found!");
  }
})();
