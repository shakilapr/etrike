import { defineConfig } from "@playwright/test";

export default defineConfig({
  testDir: "./tests",
  testMatch: "**/*.spec.ts",
  timeout: 30000,
  use: {
    baseURL: "http://127.0.0.1:5173",
  },
  webServer: [
    {
      command: "npm run dev --prefix ../backend",
      port: 3000,
      reuseExistingServer: true,
    },
    {
      command: "npm run dev --prefix ../ui",
      port: 5173,
      reuseExistingServer: true,
    },
  ],
});
