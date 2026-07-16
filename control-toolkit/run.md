# Control Toolkit — how to run

Local bench UI for eTrike: **FastAPI** backend + **React/Vite** frontend.  
Use **two dedicated terminals** you leave open. Do not rely on short-lived agent shells (they can kill processes on timeout).

| Service | Port | Role |
|---------|------|------|
| Backend (uvicorn) | **8001** | REST + WebSocket `/api/v1/*` |
| Frontend (Vite) | **5173** | UI; proxies `/api` → `127.0.0.1:8001` |

Open the app at: **http://127.0.0.1:5173/**

---

## Prerequisites

- Python **≥ 3.11**
- Node.js + npm
- Repo root on disk so the monorepo `protocol/` package resolves (backend imports it via path fix-up)

### Backend install (once)

```powershell
cd C:\projects\etrike\control-toolkit\backend
python -m venv .venv
.\.venv\Scripts\activate
pip install -e ".[dev]"
```

### Frontend install (once)

```powershell
cd C:\projects\etrike\control-toolkit\frontend
npm install
```

---

## Run (daily)

### Terminal 1 — backend

```powershell
cd C:\projects\etrike\control-toolkit\backend
# optional: activate venv if you use one
# .\.venv\Scripts\activate
python -m uvicorn control_toolkit.main:app --host 127.0.0.1 --port 8001
```

Leave this window open. Closing it stops the API → UI shows **Offline / Lost** or **502**.

Optional helper (Windows, logs under `backend/`):

```powershell
# From backend folder
.\ _start_uvicorn8001.cmd
```

(Or: `cmd /c C:\projects\etrike\control-toolkit\backend\_start_uvicorn8001.cmd`)

### Terminal 2 — frontend

```powershell
cd C:\projects\etrike\control-toolkit\frontend
$env:CTK_E2E_API = "http://127.0.0.1:8001"
npm run dev -- --host 127.0.0.1 --port 5173 --strictPort
```

- `CTK_E2E_API` must point at the toolkit backend (**8001**).
- `--strictPort` fails if something else already owns 5173 (often **control-ui**).

### Browser

1. Open **http://127.0.0.1:5173/**
2. Confirm top bar: Stream **Live**, health not **Offline**
3. **Computer** (blue) = dual virtual CAN; **Real** (amber) = CANalyst-II (needs adapter)

---

## Health checks

```powershell
# API
Invoke-WebRequest http://127.0.0.1:8001/api/v1/status -UseBasicParsing

# Vite proxy to API (must be 200 if both are up)
Invoke-WebRequest http://127.0.0.1:5173/api/v1/status -UseBasicParsing
```

| Result | Meaning |
|--------|---------|
| `:8001` fails | Start uvicorn (Terminal 1) |
| `:8001` OK, `:5173/api` 502 | Vite proxy target wrong or Vite down |
| Both OK, UI Offline | Hard refresh (`Ctrl+Shift+R`); confirm URL is toolkit on 5173 |

OpenAPI docs: **http://127.0.0.1:8001/docs**

---

## Profiles / transport

| Mode | Profile | Bus |
|------|---------|-----|
| **Computer** | `pure_software` | Dual **virtual** High/Low (no USB) |
| **Real** | `bench_test` (or `full_vehicle`) | **CANalyst-II** CH0=High, CH1=Low @ 500 kbit/s |

- Toggle in the **top bar** or **Settings**.
- No silent physical → virtual fallback. Real without adapter fails honestly.
- Env overrides (optional): `CTK_HOST`, `CTK_PORT`, `CTK_PROFILE`.

---

## Quick operate checklist

1. Start backend + frontend (above).
2. Open UI → Stream **Live**.
3. **Control** or **Drive**: turn **Bench TX ON** before inject / keyboard / low-bus streams.
4. High path: Host kinematics `HOST_DRIVE_CMD` 0x300.  
   Low path: direct motor / steer / brake (exclusive with high).
5. Second Chrome tab can **observe** (Overview / Live CAN) while the first **controls** — keep the operator tab on Drive/Control so TX is not released on leave.

---

## Tests

### Backend unit / integration

```powershell
cd C:\projects\etrike\control-toolkit\backend
pytest -q
```

### Live API probe (backend already running)

```powershell
cd C:\projects\etrike\control-toolkit\backend
python scripts/control_drive_probe.py
```

### Frontend e2e (Playwright)

```powershell
cd C:\projects\etrike\control-toolkit\frontend
$env:CTK_E2E_API = "http://127.0.0.1:8001"
npx playwright test e2e/smoke.spec.ts --reporter=list
# broader:
npx playwright test e2e/control-drive.spec.ts e2e/ui-issues-audit.spec.ts --reporter=list
```

---

## Common problems

| Symptom | Cause | Fix |
|---------|--------|-----|
| **Offline** / **Lost** + **502** | Nothing on 8001 | Start Terminal 1 (uvicorn) |
| **502** with Vite up | Proxy cannot reach API | Check `CTK_E2E_API` / port 8001 |
| Wrong UI / weird API | **control-ui** on same ports | Kill other Vite; use toolkit only on 5173 |
| Real mode fails | No CANalyst | Stay on **Computer** or plug adapter |
| Inject / keyboard 409 | Bench TX off, or ownership | Enable Bench TX; Stop all / re-arm |
| Drive TX stops when leaving tab | Safety: unmount releases control | Keep Drive tab open while operating |

---

## Do not confuse with

| Path | Notes |
|------|--------|
| `control-ui/` | Sibling product — different ports; **do not** share **5173** with toolkit |
| Backend README port **8000** | Older default; **this UI expects 8001** via Vite proxy |

---

## Useful URLs

| URL | Description |
|-----|-------------|
| http://127.0.0.1:5173/ | Control Toolkit UI |
| http://127.0.0.1:8001/api/v1/status | Backend health |
| http://127.0.0.1:8001/docs | OpenAPI |
| ws://127.0.0.1:8001/api/v1/stream | Live state (browser uses proxied path via Vite) |
