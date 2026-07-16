import { chromium } from 'playwright';

(async () => {
  const browser = await chromium.launch({ headless: true });
  const context = await browser.newContext({ viewport: { width: 1280, height: 720 } });
  const page = await context.newPage();
  
  console.log("Navigating to Control-UI...");
  await page.goto('http://localhost:5174/');
  
  // Wait for the app shell to load
  await page.waitForSelector('#app-shell');
  
  // Take a full page screenshot
  console.log("Taking screenshot...");
  await page.screenshot({ path: 'qa_screenshot.png', fullPage: true });
  
  // Check that sidebar is rendered
  const sidebarVisible = await page.isVisible('.sidebar');
  console.log('Sidebar visible:', sidebarVisible);
  
  // Check telemetry HUD
  const speed = await page.textContent('#tel-v');
  console.log('Initial Speed telemetry:', speed);
  
  // Click Drive tab
  await page.click('text=Drive');
  console.log("Clicked Drive tab.");
  
  await browser.close();
  console.log("QA script complete!");
})();
