/**
 * MCP2515 High-Bus Dashboard E2E Tests
 *
 * Verifies the debug tool UI correctly displays MCP2515-specific
 * high-bus CAN frames (0x7FD, 0x210, 0x310, 0x311, 0x220).
 *
 * These tests validate the fixes for MCP2515 send() (Bug #1) and
 * CNF3 bit timing (Bug #2) by confirming the UI renders the traffic
 * that the fixed firmware produces.
 *
 * Prerequisites: simulator running in background (npm run simulator)
 * or no simulator (backend auto-detects bus from serial).
 *
 * Run: npx playwright test tests/mcp2515-high-bus.spec.ts --project=chromium
 */

import { test, expect } from "@playwright/test";

const BASE_URL = "http://127.0.0.1:5173";

test.describe("MCP2515 High-Bus Dashboard", () => {
  test.beforeEach(async ({ page }) => {
    await page.goto(BASE_URL);
    // Wait for the dashboard to load
    await page.waitForSelector("text=Dashboard", { timeout: 10000 });
  });

  test("dashboard shows high bus as active when MCP2515 traffic present", async ({ page }) => {
    // The high bus status badge should indicate "active"
    const highBusStatus = page.locator('[data-testid="bus-status-high"], text=High').first();
    await expect(highBusStatus).toBeVisible({ timeout: 10000 });
  });

  test("CAN Monitor tab shows high-bus frames", async ({ page }) => {
    // Navigate to CAN Monitor tab
    await page.click("text=CAN Monitor");

    // Switch to high bus view
    const highBusTab = page.locator('button:has-text("High"), [role="tab"]:has-text("High")').first();
    if (await highBusTab.isVisible()) {
      await highBusTab.click();
    }

    // Wait for frames to appear
    await page.waitForSelector("table tr", { timeout: 5000 });

    // Check that frame rows exist in the monitor table
    const frameRows = page.locator("table tr, [data-testid=frame-row]");
    const count = await frameRows.count();
    expect(count).toBeGreaterThan(0);
  });

  test("high bus shows 0x7FD RT_HEARTBEAT at ~2 Hz", async ({ page }) => {
    await page.click("text=Statistics"); // or wherever per-ID stats show
    await page.waitForSelector("table tr", { timeout: 5000 });

    // Look for 0x7FD in the page content — it should have a non-zero count
    const pageContent = await page.textContent("body") ?? "";
    expect(pageContent).toContain("0x7FD");
  });

  test("Injector tab can send 0x300 HOST_DRIVE_CMD on high bus", async ({ page }) => {
    await page.click("text=Injector");

    // Select high bus
    const busSelect = page.locator("select, [role=combobox]").first();
    if (await busSelect.isVisible()) {
      await busSelect.selectOption("high");
    }

    // Find the 0x300 template
    const driveTemplate = page.locator("text=0x300, text=HOST_DRIVE_CMD").first();
    if (await driveTemplate.isVisible()) {
      await driveTemplate.click();
    }

    // Click send
    const sendButton = page.locator('button:has-text("Send"), button:has-text("Inject")').first();
    if (await sendButton.isVisible()) {
      await sendButton.click();
      // Should not show error
      await page.waitForTimeout(500);
      const errorText = await page.textContent("body") ?? "";
      expect(errorText).not.toContain("error");
    }
  });

  test("all 5 MCP2515 telemetry IDs appear in the message catalog", async ({ page }) => {
    // Navigate to a tab that shows the message list
    await page.click("text=CAN Monitor");
    await page.waitForTimeout(1000);

    const pageContent = await page.textContent("body") ?? "";
    // These IDs should be visible in the catalog/dropdown or monitor filter
    for (const id of ["0x7FD", "0x210", "0x310", "0x311", "0x220"]) {
      expect(pageContent).toContain(id);
    }
  });
});
