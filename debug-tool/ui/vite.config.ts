/// <reference types="vitest" />
import { svelte, vitePreprocess } from "@sveltejs/vite-plugin-svelte";
import { defineConfig, loadEnv } from "vite";
import path from "path";

declare const process: { cwd(): string };

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), "");
  const apiHost = env.VITE_API_HOST ?? "127.0.0.1";
  const apiPort = env.VITE_API_PORT ?? "3000";

  return {
    plugins: [svelte({ preprocess: vitePreprocess({ script: true }) })],
    resolve: {
      alias: {
        "@etrike/debug-shared": path.resolve(process.cwd(), "../shared/src/index.ts")
      }
    },
    server: {
      port: 5173,
      fs: {
        allow: [
          // Allow serving files from one level up to the project root
          "../../.."
        ]
      },
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
