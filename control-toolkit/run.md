# Control Toolkit — how to run

Local bench UI for eTrike: **FastAPI** backend + **React/Vite** frontend.  
Use **two dedicated terminals** you leave open. Do not rely on short-lived agent shells (they can kill processes on timeout).

| Service | Address | Role |
|---------|---------|------|
| Backend (uvicorn) | **http://127.0.0.1:8001** | REST + WebSocket `/api/v1/*` |
| Frontend (Vite) | **http://127.0.0.1:5173** | UI; proxies `/api` → `http://127.0.0.1:8001` |

Always bind with an **explicit host** (`127.0.0.1`), not an omitted host / ambiguous `localhost` only.

Open the app at: **http://127.0.0.1:5173/**

**Testing (software-only, unit tests, 10 s motion observation):** see [`docs/testing-guide.md`](docs/testing-guide.md).

---

## Run from monorepo root (recommended)

Repo root: `C:\projects\etrike` (or your clone path).

### Terminal 1 — API

```powershell
cd C:\projects\etrike
npm run toolkit:api
```

Same thing without npm:

```powershell
cd C:\projects\etrike
pwsh -File .\control-toolkit\scripts\start-api.ps1
```

Listens on **`127.0.0.1:8001`**.

- Status: http://127.0.0.1:8001/api/v1/status  
- Docs: http://127.0.0.1:8001/docs  
- Stream: ws://127.0.0.1:8001/api/v1/stream  

Leave this window open.

### Terminal 2 — UI

```powershell
cd C:\projects\etrike
npm run toolkit:ui
```

Or:

```powershell
cd C:\projects\etrike
pwsh -File .\control-toolkit\scripts\start-ui.ps1
```

Listens on **`127.0.0.1:5173`**, proxies to **`http://127.0.0.1:8001`**.

Open: **http://127.0.0.1:5173/**

### Optional address overrides

| Variable | Default | Used by |
|----------|---------|---------|
| `CTK_HOST` | `127.0.0.1` | API bind host |
| `CTK_PORT` | `8001` | API bind port |
| `CTK_E2E_API` | `http://127.0.0.1:8001` | Vite proxy target (full URL) |
| `CTK_UI_HOST` | `127.0.0.1` | Vite bind host |
| `CTK_UI_PORT` | `5173` | Vite bind port |

Example (custom API port — keep proxy in sync):

```powershell
# Terminal 1
$env:CTK_PORT = "8001"
$env:CTK_HOST = "127.0.0.1"
npm run toolkit:api

# Terminal 2
$env:CTK_E2E_API = "http://127.0.0.1:8001"
$env:CTK_UI_HOST = "127.0.0.1"
$env:CTK_UI_PORT = "5173"
npm run toolkit:ui
```

---

## Prerequisites

- Python **≥ 3.11**
- Node.js + npm
- Monorepo root present so the generated `protocol/` package resolves

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

## Run from package folders (equivalent)

### Terminal 1 — backend

```powershell
cd C:\projects\etrike\control-toolkit\backend
python -m uvicorn control_toolkit.main:app --host 127.0.0.1 --port 8001
```

### Terminal 2 — frontend

```powershell
cd C:\projects\etrike\control-toolkit\frontend
$env:CTK_E2E_API = "http://127.0.0.1:8001"
npm run dev -- --host 127.0.0.1 --port 5173 --strictPort
```

`npm run dev` alone is **not** enough: it only starts Vite. The API must already be on **127.0.0.1:8001**.

---

## Browser

1. Open **http://127.0.0.1:5173/**
2. Top bar: Stream **Live**, not **Offline**
3. **Computer** (blue) = dual virtual CAN; **Real** (amber) = CANalyst-II

---

## Health checks

```powershell
# API (direct address)
Invoke-WebRequest http://127.0.0.1:8001/api/v1/status -UseBasicParsing

# Same API via Vite proxy
Invoke-WebRequest http://127.0.0.1:5173/api/v1/status -UseBasicParsing
```

| Result | Meaning |
|--------|---------|
| `127.0.0.1:8001` fails | Start API (`npm run toolkit:api`) |
| API OK, `5173/api` 502 | UI not running or `CTK_E2E_API` wrong |
| Both OK, UI Offline | Hard refresh; confirm URL is **127.0.0.1:5173** |

---

## Stop (no terminal window needed)

```powershell
foreach ($port in 8001, 5173) {
  $pids = (Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue).OwningProcess |
    Select-Object -Unique
  foreach ($id in $pids) {
    if ($id) {
      Write-Host "Stopping PID $id (port $port)"
      Stop-Process -Id $id -Force -ErrorAction SilentlyContinue
    }
  }
}
```

Or **Ctrl+C** in each terminal if you still have them.

---

## Profiles / transport

| Mode | Profile | Bus |
|------|---------|-----|
| **Computer** | `pure_software` | Dual **virtual** High/Low |
| **Real** | `bench_test` / `full_vehicle` | **CANalyst-II** |

No silent physical → virtual fallback.

---

## Tests

```powershell
# From monorepo root — backend tests
cd C:\projects\etrike\control-toolkit\backend
pytest -q

# Live API probe (API must be up at 127.0.0.1:8001)
python scripts\control_drive_probe.py

# Playwright (API + UI should be up, or Playwright may start its own API)
cd C:\projects\etrike\control-toolkit\frontend
$env:CTK_E2E_API = "http://127.0.0.1:8001"
npx playwright test e2e/smoke.spec.ts --reporter=list
```

---

## Common problems

| Symptom | Cause | Fix |
|---------|--------|-----|
| Offline / 502 | Nothing on **127.0.0.1:8001** | `npm run toolkit:api` from root |
| `npm run dev` only | UI without API | Always start API first |
| Port in use | control-ui or old Vite on 5173 | Free port or use stop snippet above |
| Real mode fails | No CANalyst | Stay on **Computer** |

---

## Do not confuse with

| Path | Notes |
|------|--------|
| `control-ui/` | Sibling app; different ports; do not share **5173** with toolkit |
| Backend default config port **8000** | Toolkit UI + scripts use **8001** |

---

## Address quick reference

| What | URL |
|------|-----|
| UI | http://127.0.0.1:5173/ |
| API status | http://127.0.0.1:8001/api/v1/status |
| API docs | http://127.0.0.1:8001/docs |
| API stream | ws://127.0.0.1:8001/api/v1/stream |
| Proxied API (via Vite) | http://127.0.0.1:5173/api/v1/status |

## API reference

Endpoint dictionary (methods, bodies, gates, UI mapping):

- **[docs/api-dictionary.md](docs/api-dictionary.md)**
