import { defineConfig, devices } from '@playwright/test';

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
    video: 'retain-on-failure',
  },
  projects: [
    {
      name: 'chromium',
      use: { ...devices['Desktop Chrome'] },
    },
  ],
  webServer: [
    {
      command: 'npm run dev',
      url: 'http://localhost:5173',
      reuseExistingServer: !process.env.CI,
      timeout: 120 * 1000,
    },
    {
      command: process.platform === 'win32' 
        ? 'cd ../backend && .venv\\Scripts\\Activate.ps1 && uvicorn main:app --port 8000'
        : 'cd ../backend && uvicorn main:app --port 8000',
      url: 'http://localhost:8000/api/stream', // Wait for this URL to be ready
      reuseExistingServer: !process.env.CI,
      timeout: 120 * 1000,
    }
  ],
});
