import { chromium } from 'playwright';

(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  
  try {
    await page.goto('http://127.0.0.1:5174/', { timeout: 10000 });
    await page.waitForTimeout(2000); // Wait 2s for render
    await page.screenshot({ path: 'screenshot.png' });
    console.log("Saved screenshot.png");
  } catch (error) {
    console.log("Error: " + error.message);
  } finally {
    await browser.close();
  }
})();
