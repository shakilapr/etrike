# Debug Tool E2E Test Root Issues

After running the E2E tests via Playwright (`npx playwright test` in the `debug-tool/e2e` folder), 9 tests failed. The root issues underlying these failures are primarily related to recent structural and layout changes in the debug-tool UI, which have broken the test assumptions.

## 1. Tab Switching Mechanism Changed to CSS `display`
The UI now renders all 10 tab components into the DOM simultaneously and toggles their visibility using `style="display: none|block"` (in `App.svelte`). 
- Because Playwright locators match both visible and hidden elements by default, simple locators like `page.locator("h2")` or `page.locator(".bus-tabs")` now resolve to multiple elements across all the hidden tabs. 
- This results in "strict mode violations" in Playwright. For example, `locator('h2')` resolves to 23 elements, causing the `navigating to dictionary tab works` test to fail.
- **Affected tests:**
  - `navigating to dictionary tab works`
  - `navigating to injector shows bus selector`
  - `navigating to Unit Test tab shows profiles`

## 2. New Elements Break `.first()` Selectors
The addition of the new `Topbar` component introduced a `<select class="tb-mode-select">`.
- Tests that relied on `page.locator("select").first()` (such as the injector test in `mcp2515-high-bus.spec.ts`) now incorrectly target the Topbar's mode selector instead of the intended message selector in the Injector tab. Since the Topbar selector doesn't contain the expected options (like `"0x300"`), the test times out.
- **Affected tests:**
  - `Injector tab can send 0x300 HOST_DRIVE_CMD on high bus`

## 3. Structural Header/Layout Changes
The UI header was significantly overhauled (likely during the Svelte/TypeScript blocker fixes mentioned in the handoff).
- The `<h1>E-Trike Debug</h1>` and `<p class="eyebrow">Dual CAN Bus</p>` elements have been removed from the UI. The header now uses a smaller `tb-brand` layout.
- The `status strip` structure has also changed to a new diagnostic health bar format.
- Two new tabs (`Terminal` and `Emulator`) were added, bringing the total tab count to 10. The test expected exactly 8 tabs.
- **Affected tests:**
  - `page loads with dual-bus header` (fails to find `h1`)
  - `responsive layout at narrow viewport` (fails to find `h1`)
  - `all eight tabs are present` (fails because it expects 8 tabs, but finds 10)
  - `status strip shows connection state`

## Recommendation
Since I am restricted from editing code files, I recommend updating the E2E test suite in `debug-tool/e2e/tests` to:
1. Filter locators by visibility (e.g., `.filter({ hasText: '...' }).and(page.locator(':visible'))`) or scope them strictly to the active tab container.
2. Target specific inputs via test IDs (e.g., `data-testid="message-select"`) rather than relying on `locator("select").first()`.
3. Update the assertions to match the new Topbar layout (e.g., removing the `h1` assertion, updating the tab count to 10, and verifying the new health diagnostic groups).
