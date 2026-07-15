/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        background: '#0f172a', // slate-900
        surface: '#1e293b', // slate-800
        'surface-hover': '#334155', // slate-700
        primary: '#38bdf8', // sky-400
        accent: '#818cf8', // indigo-400
        error: '#f87171', // red-400
        success: '#4ade80', // green-400
        text: '#f8fafc', // slate-50
        'text-dim': '#94a3b8', // slate-400
      },
      animation: {
        'flash-row': 'flash 1.5s ease-out forwards',
        'pulse-slow': 'pulse 3s cubic-bezier(0.4, 0, 0.6, 1) infinite',
      },
      keyframes: {
        flash: {
          '0%': { backgroundColor: '#38bdf840' }, // primary with opacity
          '100%': { backgroundColor: 'transparent' },
        }
      }
    },
  },
  plugins: [],
}
