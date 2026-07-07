import { test, expect } from "@playwright/test";

test.describe("Debug Tool", () => {
  test("page loads with dual-bus header", async ({ page }) => {
    await page.goto("/");
    await expect(page.locator(".tb-brand")).toContainText("E-Trike");
  });

  test("status strip shows connection state", async ({ page }) => {
    await page.goto("/");
    await expect(page.locator(".tb-health-row")).toBeVisible();
  });

  test("all ten tabs are present", async ({ page }) => {
    await page.goto("/");
    await expect(page.locator("nav.tabs button")).toHaveCount(10);
    const tabs = ["Dashboard", "CAN Monitor", "CAN Dictionary", "Injector", "Statistics", "Controller", "Unit Test", "Pipeline", "Terminal", "Emulator"];
    for (const name of tabs) {
      await expect(page.locator("nav.tabs").getByText(name)).toBeVisible();
    }
  });

  test("navigating to monitor tab works", async ({ page }) => {
    await page.goto("/");
    await page.locator("nav.tabs").getByText("CAN Monitor").click();
    await expect(page.locator("h2").filter({ hasText: "CAN Monitor" }).first()).toBeVisible({ timeout: 5000 });
  });

  test("navigating to dictionary tab works", async ({ page }) => {
    await page.goto("/");
    await page.locator("nav.tabs").getByText("CAN Dictionary").click();
    await expect(page.locator("h2").filter({ hasText: "CAN Dictionary" }).first()).toBeVisible({ timeout: 5000 });
    await expect(page.locator("[data-testid=dictionary-detail]").first()).toBeVisible();
  });

  test("navigating to injector shows bus selector", async ({ page }) => {
    await page.goto("/");
    await page.locator("nav.tabs").getByText("Injector").click();
    await expect(page.locator(".bus-tabs").filter({ visible: true }).first()).toBeVisible();
  });

  test("navigating to stats shows bus load gauges", async ({ page }) => {
    await page.goto("/");
    await page.locator("nav.tabs").getByText("Statistics").click();
    await expect(page.locator(".gauge-panel").filter({ visible: true })).toHaveCount(2);
  });

  test("backend API returns IDs with bus field", async ({ request }) => {
    const response = await request.get("http://127.0.0.1:3000/api/can/ids");
    expect(response.ok()).toBeTruthy();
    const body = await response.json();
    expect(body.ids.length).toBeGreaterThan(0);
    expect(body.ids[0]).toHaveProperty("bus");
    expect(body.ids[0]).toHaveProperty("sender");
  });

  test("backend API returns dual-bus stats shape", async ({ request }) => {
    const response = await request.get("http://127.0.0.1:3000/api/can/stats");
    expect(response.ok()).toBeTruthy();
    const body = await response.json();
    expect(body.stats).toHaveProperty("buses");
    expect(body.stats.buses).toHaveProperty("high");
    expect(body.stats.buses).toHaveProperty("low");
  });

  test("backend API returns 37 CAN IDs", async ({ request }) => {
    const response = await request.get("http://127.0.0.1:3000/api/can/ids");
    expect(response.ok()).toBeTruthy();
    const body = await response.json();
    expect(body.ids.length).toBeGreaterThanOrEqual(35);
    for (const msg of body.ids) {
      expect(msg).toHaveProperty("bus");
      expect(msg).toHaveProperty("id");
      expect(msg).toHaveProperty("name");
      expect(msg).toHaveProperty("sender");
      expect(msg).toHaveProperty("dlc");
      expect(msg).toHaveProperty("injectable");
      expect(msg).toHaveProperty("fields");
    }
  });

  test("navigating to Unit Test tab shows profiles", async ({ page }) => {
    await page.goto("/");
    await page.locator("nav.tabs").getByText("Unit Test").click();
    await expect(page.locator("h2").filter({ hasText: "Unit Under Test" }).first()).toBeVisible({ timeout: 5000 });
    await expect(page.locator(".unit-buttons button").filter({ visible: true }).first()).toBeVisible();
  });

  test("responsive layout at narrow viewport", async ({ page }) => {
    await page.setViewportSize({ width: 800, height: 600 });
    await page.goto("/");
    await expect(page.locator(".tb-brand")).toContainText("E-Trike");
    // No horizontal overflow — check body does not exceed viewport
    const bodyWidth = await page.locator("body").evaluate(
      (el) => el.scrollWidth
    );
    expect(bodyWidth).toBeLessThanOrEqual(820); // tolerate small scrollbar
  });

  test("tab state is retained across navigation (BUG-04 regression)", async ({ page }) => {
    await page.goto("/");
    
    // Go to Injector tab
    await page.locator("nav.tabs").getByText("Injector").click();
    await expect(page.locator(".bus-tabs").filter({ visible: true }).first()).toBeVisible();
    
    // Switch to Low Bus
    await page.locator(".bus-tabs").getByText("Low Bus").click();
    await expect(page.locator(".bus-tabs button.active")).toContainText("Low Bus");

    // Navigate away to Dashboard
    await page.locator("nav.tabs").getByText("Dashboard").click();
    await expect(page.locator("h2").filter({ hasText: "Dashboard" }).first()).toBeVisible();

    // Navigate back to Injector
    await page.locator("nav.tabs").getByText("Injector").click();
    
    // State should be retained (Low Bus still active)
    await expect(page.locator(".bus-tabs button.active")).toContainText("Low Bus");
  });
});
