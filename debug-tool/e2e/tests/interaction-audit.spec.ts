import { expect, test } from "@playwright/test";

test.describe("Debug Tool interaction audit", () => {
  test.beforeEach(async ({ request }) => {
    await request.delete("http://127.0.0.1:3000/api/can/frames").catch(() => undefined);
    await request.post("http://127.0.0.1:3000/api/mode", {
      data: {
        mode: "monitor",
        simulatedEcus: [],
        idSources: {},
        injectEmulatedToPhysical: false,
        bypasses: { sesSync: false, sebSync: false, mtrAbsent: false, benchSolo: false },
      },
    }).catch(() => undefined);
  });

  test("mode selector switches the active work mode", async ({ page, request }) => {
    await page.goto("/");

    await page.locator("nav.tabs").getByRole("button", { name: "Work Mode" }).click();
    await expect(page.getByText("Work Mode Configurator")).toBeVisible();

    await page.getByRole("button", { name: "Full Simulation" }).click();
    await expect(page.getByRole("button", { name: /Apply/i })).toBeEnabled();
    const modePost = page.waitForResponse((response) =>
      response.url().endsWith("/api/mode") && response.request().method() === "POST"
    );
    await page.getByRole("button", { name: /Apply/i }).click();
    await expect((await modePost).ok()).toBeTruthy();
    await expect(page.locator(".tb-mode-badge")).toContainText("Full Simulation");

    await expect
      .poll(async () => {
        const state = await request.get("http://127.0.0.1:3000/api/mode");
        expect(state.ok()).toBeTruthy();
        return (await state.json()).mode;
      })
      .toBe("full-sim");
  });

  test("primary tabs switch without keeping hidden panels active", async ({ page }) => {
    await page.goto("/");

    const tabNames = [
      "CAN Monitor",
      "CAN Dictionary",
      "Injector",
      "Controller",
      "Unit Test",
      "Pipeline",
      "Statistics",
      "Terminal",
      "Work Mode",
      "Dashboard"
    ];

    for (const name of tabNames) {
      const startedAt = performance.now();
      await page.locator("nav.tabs").getByRole("button", { name }).click();
      const activeTab = page.locator("nav.tabs button.active");
      await expect(activeTab).toHaveText(name);
      expect(performance.now() - startedAt).toBeLessThan(1000);
    }
  });

  test("controller keyboard commands update the visible command state", async ({ page }) => {
    await page.goto("/");
    await page.locator("nav.tabs").getByRole("button", { name: "Controller" }).dispatchEvent("click");
    const controller = page.locator(".injector-layout").filter({ has: page.locator("h2", { hasText: "Controller" }) });
    await expect(controller).toBeVisible();

    await controller.getByRole("button", { name: "Start" }).dispatchEvent("click");
    await page.keyboard.down("w");
    await expect(controller.locator(".ctrl-metric").filter({ hasText: "Speed" }).locator("strong")).toContainText("2000");

    await page.keyboard.down("a");
    await expect(controller.locator(".ctrl-metric").filter({ hasText: "Yaw rate" }).locator("strong")).toContainText("-87");

    await page.keyboard.down("b");
    await expect(controller.locator(".ctrl-metric").filter({ hasText: "Brake" }).locator("strong")).toContainText("ON");

    await page.keyboard.up("b");
    await page.keyboard.up("a");
    await page.keyboard.up("w");
    await page.keyboard.press("Tab");
    await expect(controller.locator(".bus-tabs button.active")).toContainText(/LOW Bus/i);

    await page.keyboard.press("Escape");
    await expect(controller.locator(".ctrl-metric").filter({ hasText: "Speed" }).locator("strong")).toContainText("0");
  });
});
