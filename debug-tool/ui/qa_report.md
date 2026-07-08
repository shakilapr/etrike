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
- None! The full click-regression suite now passes 100%.

## Fixes Made & Verified
- **BUG-01 (WASD Text Field Override):** Verified that WASD inputs inside text fields no longer trigger controller overrides.
- **BUG-02 (Gear/Speed Mismatch):** Verified that W/S correctly auto-shift gears to D/R while maintaining correct speed targets.
- **BUG-70 & BUG-71:** Flex layout collapses and mobile viewport hidden elements have been successfully mitigated.

## Remaining Risks
- No critical layout or input bugs remain in the UI. Playwright tests are passing successfully on all viewports.
