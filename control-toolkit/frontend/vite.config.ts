import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// Default to control-toolkit backend (8001). Override with CTK_E2E_API.
const apiTarget = process.env.CTK_E2E_API || 'http://127.0.0.1:8001'

export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      '/api': {
        target: apiTarget,
        changeOrigin: true,
        ws: true,
      },
    },
  },
})
