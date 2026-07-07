/// <reference types="vitest" />
import { svelte, vitePreprocess } from "@sveltejs/vite-plugin-svelte";
import { defineConfig, loadEnv } from "vite";

declare const process: { cwd(): string };

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), "");
  const apiHost = env.VITE_API_HOST ?? "127.0.0.1";
  const apiPort = env.VITE_API_PORT ?? "3000";

  return {
    plugins: [svelte({ preprocess: vitePreprocess({ script: true }) })],
    server: {
      port: 5173,
      proxy: {
        "/api": `http://${apiHost}:${apiPort}`,
        "/ws": {
          target: `ws://${apiHost}:${apiPort}`,
          ws: true
        }
      }
    },
    test: {
      environment: "jsdom"
    }
  };
});
