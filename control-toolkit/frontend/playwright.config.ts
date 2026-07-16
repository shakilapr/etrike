import { defineConfig, devices } from '@playwright/test'

// Dedicated e2e ports to avoid clashes with a developer's manual servers.
const backendPort = 8010
const frontendPort = 5174
const backend = `http://127.0.0.1:${backendPort}`
const frontend = `http://127.0.0.1:${frontendPort}`

export default defineConfig({
  testDir: './e2e',
  fullyParallel: false,
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 1 : 0,
  workers: 1,
  timeout: 60_000,
  expect: { timeout: 15_000 },
  reporter: [['list']],
  use: {
    baseURL: frontend,
    trace: 'on-first-retry',
    screenshot: 'only-on-failure',
  },
  projects: [{ name: 'chromium', use: { ...devices['Desktop Chrome'] } }],
  webServer: [
    {
      command: `python -m uvicorn control_toolkit.main:app --host 127.0.0.1 --port ${backendPort}`,
      cwd: '../backend',
      url: `${backend}/api/v1/status`,
      reuseExistingServer: false,
      timeout: 120_000,
    },
    {
      // Point Vite proxy at e2e backend port via env (vite.config reads it).
      command: `npm run dev -- --host 127.0.0.1 --port ${frontendPort}`,
      url: frontend,
      reuseExistingServer: false,
      timeout: 120_000,
      env: {
        ...process.env,
        CTK_E2E_API: backend,
      },
    },
  ],
})
