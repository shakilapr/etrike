import { chromium } from 'playwright';

(async () => {
  const browser = await chromium.launch({ headless: true });
  const context = await browser.newContext({ viewport: { width: 1280, height: 720 } });
  const page = await context.newPage();
  
  // Intercept console logs to debug
  page.on('console', msg => console.log('BROWSER LOG:', msg.text()));
  
  console.log("Navigating to Control-UI...");
  await page.goto('http://localhost:5174/');
  
  await page.waitForSelector('#app-shell');
  await page.waitForTimeout(2000); // Wait for multiple frames
  
  await browser.close();
  console.log("QA script complete!");
})();
