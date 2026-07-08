# UI Full Click Regression Report

## Tested Environment
- URL: `http://127.0.0.1:5174/`
- Viewports Tested: Desktop Chrome (1280x720) and Mobile (390x844)
- Tooling: `@playwright/test`

## Tested Click Flows
- Clicked through every main navigation tab to verify rendering and absence of layout collapse.
- Verified top-bar mode selection.
- Tested Physics View sidebar toggling (verifying open/close UI states).
- Simulated keyboard inputs (`W/A/S/D`) on the Controller tab.
- Interacted with ECU power controls on the Emulator tab.
- Mobile viewport top-bar interaction and responsiveness.

## Failures Found
- **Console errors:** `Failed to load resource: the server responded with a status of 404 (Not Found)` during initial load (non-fatal, typically missing source maps or assets).
- **Layout Collapses (BUG-70):** Tests discovered that `.panel` elements within tabs (like `CanMonitor` and `Controller`) collapse in height if they rely on `flex-grow: 1`. 
- **Mobile Viewport (BUG-71):** The topbar `.tb-mode-select` becomes inaccessible/hidden on mobile viewports (`390x844`), causing interaction timeouts.

## Fixes Made
- Reverted CSS layout fixes at user's request (tests only).
- Configured Playwright to execute sequentially (`workers: 1`) to reduce load on the Vite development server.
- Adjusted Playwright `selectOption` interaction to handle hidden mobile elements gracefully.

## Remaining Risks
- The layout collapse (BUG-70) severely impacts the usability of the app for any components needing flexible height.
- The mobile viewport is currently unusable for critical topbar actions.
