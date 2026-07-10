import { expect, test } from "@playwright/test";

const MODES = ["monitor", "full-sim", "emulator", "hybrid", "bench"] as const;
const TABS = [
  "Dashboard",
  "CAN Monitor",
  "CAN Dictionary",
  "Injector",
  "Controller",
  "Unit Test",
  "Pipeline",
  "Statistics",
  "Terminal",
  "Work Mode",
] as const;

test.describe("No-hardware work mode usability audit", () => {
  test.beforeEach(async ({ request }) => {
    await request.delete("http://127.0.0.1:3000/api/can/frames").catch(() => undefined);
    await request.post("http://127.0.0.1:3000/api/mode", { data: modeConfig("bench") }).catch(() => undefined);
  });

  test("modes, tabs, commands, and health bar remain usable without hardware", async ({ page, request }) => {
    test.setTimeout(120000);
    const browserErrors: string[] = [];
    page.on("pageerror", (error) => browserErrors.push(error.message));
    page.on("console", (message) => {
      if (message.type() === "error") browserErrors.push(message.text());
    });

    await page.goto("/");
    await expect(page.getByTestId("topbar-mode-badge")).toBeVisible();
    await page.getByTestId("main-tabs").getByRole("button", { name: "Work Mode" }).click();
    await expect(page.getByText("Work Mode Configurator")).toBeVisible();
    await expect.poll(async () => {
      const apiMode = await request.get("http://127.0.0.1:3000/api/mode");
      if (!apiMode.ok()) return "api-unavailable";
      return (await apiMode.json()).mode;
    }).toBe("bench");
    await expect(page.getByTestId("topbar-health-row")).toBeVisible();

    for (const mode of MODES) {
      await applyMode(page, mode);

      await page.waitForTimeout(mode === "full-sim" ? 1200 : 3600);
      const apiMode = await request.get("http://127.0.0.1:3000/api/mode");
      expect(apiMode.ok(), `${mode} GET /api/mode should succeed`).toBe(true);
      expect((await apiMode.json()).mode).toBe(mode);

      const snapshot = await readHeaderSnapshot(page);
      console.log(`[${mode}] health=${snapshot.health} state=${snapshot.vehicleState} telemetry=${snapshot.telemetry} overflow=${snapshot.overflowPx}`);
      expect(snapshot.overflowPx, `${mode} should not create horizontal overflow`).toBeLessThanOrEqual(8);

      if (mode === "full-sim") {
        await expect.poll(async () => (await readHeaderSnapshot(page)).health).toContain("ready");
      } else {
        expect(snapshot.health, `${mode} should show no ECUs without hardware`).toContain("0/5 lost");
        expect(snapshot.telemetry, `${mode} should not show stale telemetry without hardware`).toContain("No data");
      }
    }

    for (const tab of TABS) {
      const started = Date.now();
      await page.getByTestId("main-tabs").getByRole("button", { name: tab }).click();
      await expect(page.getByTestId("main-tabs").locator("button.active")).toHaveText(tab);
      const elapsed = Date.now() - started;
      console.log(`[tab] ${tab} ${elapsed}ms`);
      expect(elapsed, `${tab} should switch promptly`).toBeLessThan(1000);
    }

    await applyMode(page, "full-sim");
    await page.getByTestId("main-tabs").getByRole("button", { name: "Controller" }).click();
    const controller = page.locator(".injector-layout").filter({ has: page.locator("h2", { hasText: "Controller" }) });
    await expect(controller).toBeVisible();
    await controller.getByRole("button", { name: "Start" }).click();

    await page.keyboard.down("w");
    await expect(controller.locator(".ctrl-metric").filter({ hasText: "Speed" }).locator("strong")).toContainText("2000");
    await page.waitForTimeout(800);
    console.log(`[command W] ${JSON.stringify(await readHeaderSnapshot(page))}`);

    await page.keyboard.down("a");
    await expect(controller.locator(".ctrl-metric").filter({ hasText: "Yaw rate" }).locator("strong")).toContainText("-87");
    await page.waitForTimeout(500);
    console.log(`[command A] ${JSON.stringify(await readHeaderSnapshot(page))}`);

    await page.keyboard.down("b");
    await expect(controller.locator(".ctrl-metric").filter({ hasText: "Brake" }).locator("strong")).toContainText("ON");
    await page.waitForTimeout(500);
    console.log(`[command B] ${JSON.stringify(await readHeaderSnapshot(page))}`);

    await page.keyboard.up("b");
    await page.keyboard.up("a");
    await page.keyboard.up("w");
    await page.keyboard.press("Escape");
    await expect(controller.locator(".ctrl-metric").filter({ hasText: "Speed" }).locator("strong")).toContainText("0");

    expect(browserErrors).toEqual([]);
  });
});

function modeConfig(mode: (typeof MODES)[number]) {
  return {
    mode,
    simulatedEcus: mode === "full-sim" ? ["host", "rt", "sys", "mtr", "ses", "seb"] : [],
    idSources: {},
    injectEmulatedToPhysical: false,
    bypasses: { sesSync: false, sebSync: false, mtrAbsent: false, benchSolo: false },
  };
}

async function applyMode(page: import("@playwright/test").Page, mode: (typeof MODES)[number]) {
  const labels: Record<(typeof MODES)[number], string> = {
    "full-sim": "Full Simulation",
    emulator: "Emulator",
    hybrid: "Hybrid",
    bench: "Bench Test",
    monitor: "Monitor Only",
  };
  await page.getByTestId("main-tabs").getByRole("button", { name: "Work Mode" }).click();
  await page.getByRole("button", { name: labels[mode], exact: true }).click();
  const applyButton = page.getByRole("button", { name: /Apply/i });
  await expect(applyButton, `${mode} should become dirty before applying`).toBeEnabled();
  const responsePromise = page.waitForResponse((response) =>
    response.url().endsWith("/api/mode") && response.request().method() === "POST"
  );
  await applyButton.click();
  const response = await responsePromise;
  expect(response.ok(), `${mode} POST /api/mode should succeed`).toBe(true);
  await expect(page.getByTestId("topbar-mode-badge")).toContainText(labels[mode]);
}

async function readHeaderSnapshot(page: import("@playwright/test").Page) {
  return page.evaluate(() => {
    const text = (selector: string) => document.querySelector(selector)?.textContent?.replace(/\s+/g, " ").trim() ?? "";
    return {
      health: text(".tb-health"),
      vehicleState: text(".tb-vstate"),
      telemetry: text(".tb-telem"),
      overflowPx: Math.max(0, document.documentElement.scrollWidth - window.innerWidth),
    };
  });
}
