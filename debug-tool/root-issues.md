# E2E Test Failures Analysis

The recent debug tool UI cleanup introduced breaking changes to the DOM structure that caused 9 out of 17 Playwright E2E tests to fail. The unit tests and type checks continue to pass because they run against individual components or isolated stores. However, the E2E suite verifies the assembled page layout, which has fundamentally changed.

Below is an in-depth analysis of each failed test and the specific code adjustments required to fix them.

---

### 1. Header & Branding Structure Changed
**Failing Tests:**
- `page loads with dual-bus header`
- `responsive layout at narrow viewport`

**Analysis:**
The test explicitly looks for an `<h1>` containing "E-Trike Debug" and a paragraph with class `.eyebrow` containing "Dual CAN Bus". During the cleanup, `App.svelte` was refactored to use a new `Topbar.svelte` component. This new component replaces the standard `<h1>` and `.eyebrow` elements with a more compact header (`.tb-brand`).

**Proposed Fix for `debug-tool.spec.ts`:**
```typescript
test("page loads with dual-bus header", async ({ page }) => {
  await page.goto("/");
  // Instead of h1, check the new Topbar brand element
  await expect(page.locator(".tb-brand")).toContainText("E-Trike");
});

test("responsive layout at narrow viewport", async ({ page }) => {
  await page.setViewportSize({ width: 800, height: 600 });
  await page.goto("/");
  // Instead of h1, check the new Topbar brand element
  await expect(page.locator(".tb-brand")).toContainText("E-Trike");
  
  const bodyWidth = await page.locator("body").evaluate((el) => el.scrollWidth);
  expect(bodyWidth).toBeLessThanOrEqual(820);
});
```

---

### 2. Tab Navigation & DOM Visibility (Strict Mode Violations)
**Failing Tests:**
- `navigating to dictionary tab works`
- `navigating to injector shows bus selector`
- `navigating to Unit Test tab shows profiles`

**Analysis:**
Previously, `App.svelte` conditionally rendered tabs using `{#if activeTab === '...'}`. Now, all tabs are rendered into the DOM simultaneously and toggled using inline styles (`style="display: none"` or `style="display: block"`). 
Because Playwright locators match elements regardless of their CSS visibility (unless explicitly filtered), a generic locator like `page.locator("h2")` now finds all `<h2>` elements across all 10 tabs (resulting in 23 matches), triggering a Playwright strict mode violation. 

**Proposed Fix for `debug-tool.spec.ts`:**
Use Playwright's `getByRole` which inherently filters better, or chain `.filter({ state: 'visible' })` / `.first()`.

```typescript
test("navigating to dictionary tab works", async ({ page }) => {
  await page.goto("/");
  await page.locator("nav.tabs").getByText("CAN Dictionary").click();
  // Filter heading strictly to visible ones or specific text
  await expect(page.getByRole("heading", { name: "CAN Dictionary" })).toBeVisible();
  await expect(page.locator("[data-testid=dictionary-detail]").first()).toBeVisible();
});

test("navigating to injector shows bus selector", async ({ page }) => {
  await page.goto("/");
  await page.locator("nav.tabs").getByText("Injector").click();
  // Filter for visible bus-tabs elements
  await expect(page.locator(".bus-tabs").filter({ state: "visible" }).first()).toBeVisible();
});

test("navigating to Unit Test tab shows profiles", async ({ page }) => {
  await page.goto("/");
  await page.locator("nav.tabs").getByText("Unit Test").click();
  await expect(page.getByRole("heading", { name: "Unit Under Test" })).toBeVisible();
  await expect(page.locator(".unit-buttons button").filter({ state: "visible" }).first()).toBeVisible();
});
```

---

### 3. Additional Tabs and New Diagnostic Status Strip
**Failing Tests:**
- `all eight tabs are present`
- `status strip shows connection state`

**Analysis:**
The test expects exactly 8 tabs, but the recent cleanup added two new tabs: `Terminal` and `Emulator`, bringing the total to 10.
Additionally, the `.status-strip` class no longer exists. It has been completely replaced by a structured diagnostic `.tb-health-row` inside the `Topbar`.

**Proposed Fix for `debug-tool.spec.ts`:**
```typescript
test("status strip shows connection state", async ({ page }) => {
  await page.goto("/");
  // .status-strip was removed; look for the new diagnostic row
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
```

---

### 4. Vague Selectors Matching New Topbar Elements
**Failing Tests:**
- `Injector tab can send 0x300 HOST_DRIVE_CMD on high bus` (Timeout)

**Analysis:**
The test `mcp2515-high-bus.spec.ts` tries to select a CAN ID from a dropdown using the generic locator: `page.locator("select").first()`.
Before the `Topbar` was introduced, the first `<select>` element on the page was indeed the injector's CAN ID dropdown. Now, the first `<select>` element is the work-mode selector (`.tb-mode-select`) located in the Topbar, which does not contain the option `"0x300"`. Playwright waits for the option to appear and eventually times out.

**Proposed Fix for `mcp2515-high-bus.spec.ts`:**
Target the `<select>` element specifically within the injector tab, or target by name/label.

```typescript
test("Injector tab can send 0x300 HOST_DRIVE_CMD on high bus", async ({ page }) => {
  await page.click("text=Injector");
  await page.getByRole("button", { name: "HIGH Bus" }).click();

  // Explicitly scope the select to the visible injector form or filter by value
  const injectorSelect = page.locator(".injector-panel select").first();
  await injectorSelect.selectOption("0x300");
  
  await expect(page.locator("body")).toContainText("HOST_DRIVE_CMD");

  test.skip(!(await hasOpenLink(page)), "MCP2515/serial link is not open in this environment.");

  const sendButton = page.locator('button:has-text("Send"), button:has-text("Inject")').filter({ state: "visible" }).first();
  if (await sendButton.isVisible()) {
    await sendButton.click();
    await page.waitForTimeout(500);
    const errorText = await page.textContent("body") ?? "";
    expect(errorText).not.toContain("error");
  }
});
```

---

### Summary
To resolve all E2E issues, `debug-tool.spec.ts` and `mcp2515-high-bus.spec.ts` need to be updated with the code blocks above. Because I am restricted by project rules from modifying source code files directly, this analysis serves as a comprehensive guide for applying the necessary adjustments to the test suite.
