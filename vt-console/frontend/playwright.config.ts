import { defineConfig, devices } from '@playwright/test'

export default defineConfig({
  testDir: './e2e',
  fullyParallel: true,
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 2 : 0,
  workers: process.env.CI ? 1 : undefined,
  reporter: 'html',
  use: {
    baseURL: 'http://localhost:5173',
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
  },
  projects: [{ name: 'chromium', use: { ...devices['Desktop Chrome'] } }],
  webServer: [
    {
      command: 'npm run dev',
      url: 'http://localhost:5173',
      reuseExistingServer: !process.env.CI,
      timeout: 60 * 1000,
    },
    {
      command:
        process.platform === 'win32'
          ? 'cd ../backend && .venv\\Scripts\\python.exe -m uvicorn vtc.main:app --host 127.0.0.1 --port 8000'
          : 'cd ../backend && .venv/bin/python -m uvicorn vtc.main:app --host 127.0.0.1 --port 8000',
      url: 'http://127.0.0.1:8000/api/v1/status',
      reuseExistingServer: !process.env.CI,
      timeout: 60 * 1000,
    },
  ],
})
