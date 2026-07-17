import { defineConfig, devices } from '@playwright/test'

/**
 * Audit against the developer stack already running on 5173 / 8001.
 * Does not spawn webServer — start with `npm run toolkit:api` + `toolkit:ui` first.
 */
export default defineConfig({
  testDir: './e2e',
  testMatch: '**/live-click-audit.spec.ts',
  fullyParallel: false,
  retries: 0,
  workers: 1,
  timeout: 480_000,
  expect: { timeout: 15_000 },
  reporter: [['list']],
  use: {
    baseURL: 'http://127.0.0.1:5173',
    trace: 'off',
    screenshot: 'only-on-failure',
  },
  projects: [{ name: 'chromium', use: { ...devices['Desktop Chrome'] } }],
})
