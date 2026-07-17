import { useEffect, useState } from 'react'

// Ticking clock so age displays advance even when no new WS frame arrives
// (workplan §4.3: "independent freshness clock").
export function useNow(intervalMs = 500): number {
  const [now, setNow] = useState(() => Date.now())
  useEffect(() => {
    const id = window.setInterval(() => setNow(Date.now()), intervalMs)
    return () => window.clearInterval(id)
  }, [intervalMs])
  return now
}
