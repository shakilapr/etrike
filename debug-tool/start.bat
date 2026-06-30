@echo off
echo Starting E-Trike Debug Tool...

REM Start backend (port 3000)
start "Debug Backend" cmd /c "cd /d %~dp0backend && npm run dev"

REM Wait for backend to be ready
timeout /t 3 /nobreak >nul

REM Start UI dev server (port 5173)
start "Debug UI" cmd /c "cd /d %~dp0ui && npm run dev"

timeout /t 2 /nobreak >nul

echo.
echo Backend:  http://localhost:3000
echo UI:      http://localhost:5173
echo.
echo Open http://localhost:3000 in your browser
echo Close the terminal windows to stop the servers
