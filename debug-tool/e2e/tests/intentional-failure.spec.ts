import { test, expect } from "@playwright/test";

test.describe("Intentional Failure @intentional-failure", () => {
  test.fail("fails when using a stale structural selector", async ({ page }) => {
    await page.goto("/");
    // This uses the old brittle selector that was replaced, so it should fail
    // We expect this to fail, but playright test will report it as a failure
    // We can tag this test and not run it by default, or run it explicitly
    // The requirement is "A test intentionally using a stale selector fails, proving the suite is actually executing."
    await expect(page.locator(".tb-mode-select").first()).toBeVisible({ timeout: 1000 });
  });
});
