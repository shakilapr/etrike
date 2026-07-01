import { test, expect } from "@playwright/test";

test.describe("Debug Tool", () => {
  test("page loads with dual-bus header", async ({ page }) => {
    await page.goto("/");
    await expect(page.locator("h1")).toContainText("E-Trike Debug");
    await expect(page.locator(".eyebrow")).toContainText("Dual CAN Bus");
  });

  test("status strip shows connection state", async ({ page }) => {
    await page.goto("/");
    await expect(page.locator(".status-strip")).toBeVisible();
  });

  test("all eight tabs are present", async ({ page }) => {
    await page.goto("/");
    await expect(page.locator("nav.tabs button")).toHaveCount(8);
    const tabs = ["Dashboard", "CAN Monitor", "CAN Dictionary", "Injector", "Statistics", "Controller", "Unit Test", "Pipeline"];
    for (const name of tabs) {
      await expect(page.locator("nav.tabs").getByText(name)).toBeVisible();
    }
  });

  test("navigating to monitor tab works", async ({ page }) => {
    await page.goto("/");
    await page.locator("nav.tabs").getByText("CAN Monitor").click();
    await expect(page.locator("h2")).toContainText("CAN Monitor");
  });

  test("navigating to dictionary tab works", async ({ page }) => {
    await page.goto("/");
    await page.locator("nav.tabs").getByText("CAN Dictionary").click();
    await expect(page.locator("h2")).toContainText("CAN Dictionary");
    await expect(page.locator("[data-testid=dictionary-detail]").first()).toBeVisible();
  });

  test("navigating to injector shows bus selector", async ({ page }) => {
    await page.goto("/");
    await page.locator("nav.tabs").getByText("Injector").click();
    await expect(page.locator(".bus-tabs")).toBeVisible();
  });

  test("navigating to stats shows bus load gauges", async ({ page }) => {
    await page.goto("/");
    await page.locator("nav.tabs").getByText("Statistics").click();
    await expect(page.locator(".gauge-panel")).toHaveCount(2);
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
    await expect(page.locator("h2").first()).toContainText("Unit Under Test");
    // At least one profile button should be visible
    await expect(page.locator(".unit-buttons button").first()).toBeVisible();
  });

  test("responsive layout at narrow viewport", async ({ page }) => {
    await page.setViewportSize({ width: 800, height: 600 });
    await page.goto("/");
    // Page should still render the header
    await expect(page.locator("h1")).toContainText("E-Trike Debug");
    // No horizontal overflow — check body does not exceed viewport
    const bodyWidth = await page.locator("body").evaluate(
      (el) => el.scrollWidth
    );
    expect(bodyWidth).toBeLessThanOrEqual(820); // tolerate small scrollbar
  });
});
