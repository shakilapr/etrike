import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// Default to control-toolkit backend. Override with CTK_E2E_API (full URL + host).
// Always use an explicit host in the target (127.0.0.1, not bare localhost-only docs).
const apiTarget = process.env.CTK_E2E_API || 'http://127.0.0.1:8001'
const uiHost = process.env.CTK_UI_HOST || '127.0.0.1'
const uiPort = Number(process.env.CTK_UI_PORT || 5173)

export default defineConfig({
  plugins: [react()],
  server: {
    host: uiHost,
    port: uiPort,
    strictPort: true,
    proxy: {
      '/api': {
        target: apiTarget,
        changeOrigin: true,
        ws: true,
      },
    },
  },
})
