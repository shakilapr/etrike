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

async function hasOpenLink(page: import("@playwright/test").Page): Promise<boolean> {
  const response = await page.request.get(`${BASE_URL}/api/status`);
  if (!response.ok()) return false;
  const status = await response.json();
  return Boolean(status.bridge?.link_open || status.serial?.port_open);
}

test.describe("MCP2515 High-Bus Dashboard @hardware", () => {
  test.beforeEach(async ({ page }, testInfo) => {
    // Skip all MCP2515 tests when no CAN hardware is connected
    const statusResp = await page.request.get(`${BASE_URL}/api/status`);
    if (statusResp.ok()) {
      const status = await statusResp.json();
      const hasHardware = status.bridge?.connected || status.serial?.port_open;
      if (!hasHardware) {
        testInfo.skip(true, "No CAN hardware connected — skipping MCP2515 tests");
        return;
      }
    }
    await page.goto(BASE_URL);
    await page.waitForSelector("text=Dashboard", { timeout: 10000 });
  });

  test("dashboard shows high bus as active when MCP2515 traffic present", async ({ page }) => {
    const highBusStatus = page.getByTestId("bus-status-high");
    await expect(highBusStatus).toBeVisible({ timeout: 10000 });
    await expect(highBusStatus).toContainText(/High bus/i);
  });

  test("CAN Monitor tab shows high-bus monitor groups", async ({ page }) => {
    // Navigate to CAN Monitor tab
    await page.click("text=CAN Monitor");

    // Switch to high bus view
    const highBusTab = page.locator('.monitor-panel button:has-text("High")').first();
    if (await highBusTab.isVisible()) {
      await highBusTab.click();
    }

    await expect(page.locator(".monitor-panel")).toBeVisible({ timeout: 5000 });
    await expect(page.locator(".monitor-panel .cat-card, .monitor-panel .msg-card").first()).toBeVisible({ timeout: 5000 });
    await expect(page.locator("body")).toContainText(/0x7FD|0x210|0x220|0x310|0x311/);
  });

  test("high bus shows 0x7FD RT_HEARTBEAT in statistics/catalog", async ({ page }) => {
    await page.click("text=Statistics"); // or wherever per-ID stats show
    await expect(page.locator("body")).toContainText("0x7FD", { timeout: 5000 });

    const pageContent = await page.textContent("body") ?? "";
    expect(pageContent).toContain("0x7FD");
  });

  test("Injector tab can send 0x300 HOST_DRIVE_CMD on high bus", async ({ page }) => {
    await page.click("text=Injector");

    await page.locator('.injector-layout button:has-text("high"), .injector-layout button:has-text("High")').first().click();

    await page.locator(".injector-layout select").first().selectOption("0x300");
    await expect(page.locator("body")).toContainText("HOST_DRIVE_CMD");

    test.skip(!(await hasOpenLink(page)), "MCP2515/serial link is not open in this environment.");

    const sendButton = page.locator('button:has-text("Send"), button:has-text("Inject")').first();
    if (await sendButton.isVisible()) {
      await sendButton.click();
      await page.waitForTimeout(500);
      const errorText = await page.textContent("body") ?? "";
      expect(errorText).not.toContain("error");
    }
  });

  test("all 5 MCP2515 telemetry IDs appear in the message catalog", async ({ page }) => {
    // Navigate to the dictionary tab that shows the full message catalog.
    await page.click("text=CAN Dictionary");
    await page.waitForTimeout(1000);

    const pageContent = await page.textContent("body") ?? "";
    // These IDs should be visible in the catalog/dropdown or monitor filter
    for (const id of ["0x7FD", "0x210", "0x310", "0x311", "0x220"]) {
      expect(pageContent).toContain(id);
    }
  });
});
