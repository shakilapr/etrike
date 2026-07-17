/** @type {import('tailwindcss').Config} */
export default {
  content: ['./index.html', './src/**/*.{js,ts,jsx,tsx}'],
  theme: {
    extend: {
      colors: {
        // Dark, high-contrast automotive base (architecture §17).
        bg: '#0a0e14',
        surface: '#12181f',
        'surface-raised': '#1a222c',
        'surface-hover': '#232d39',
        border: '#2a3441',
        'border-strong': '#3a4756',
        text: '#e6edf3',
        'text-dim': '#8b98a9',
        'text-faint': '#5b6b7d',
        // High/Low bus: stable, distinct accents (never reused for anything else).
        'bus-high': '#38bdf8', // sky-400
        'bus-low': '#c084fc', // purple-400
        // Freshness states.
        live: '#4ade80',
        late: '#facc15',
        missing: '#6b7688',
        invalid: '#f87171',
        frozen: '#38bdf8',
        recovering: '#fb923c',
        // Reserved for faults/ESTOP/destructive only.
        danger: '#ef4444',
        'danger-soft': '#ef444422',
      },
      fontFamily: {
        ui: ['Inter', 'ui-sans-serif', '-apple-system', 'Segoe UI', 'Roboto', 'sans-serif'],
        mono: ['JetBrains Mono', 'ui-monospace', 'Menlo', 'Consolas', 'monospace'],
      },
    },
  },
  plugins: [],
}
