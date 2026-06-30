#!/bin/bash
# Start E-Trike Debug Tool
# Run from debug-tool directory

echo "Starting E-Trike Debug Tool..."

# Start backend (port 3000)
cd "$(dirname "$0")/backend" && npm run dev &
BACKEND_PID=$!

sleep 3

# Start UI dev server (port 5173)
cd "$(dirname "$0")/ui" && npm run dev &
UI_PID=$!

sleep 2

echo ""
echo "Backend:  http://localhost:3000"
echo "UI:      http://localhost:5173"
echo "Open http://localhost:3000 in your browser"
echo "Run: kill $BACKEND_PID $UI_PID   to stop"

wait
