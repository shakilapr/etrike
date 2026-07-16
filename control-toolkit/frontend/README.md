# Control Toolkit Frontend

React + Vite UI for the Control Toolkit Pure Software backend.

## Setup

```bash
npm install
```

## Dev

Start backend first:

```bash
cd ../backend
pip install -e ".[dev]"
uvicorn control_toolkit.main:app --host 127.0.0.1 --port 8000
```

Then:

```bash
npm run dev
# http://127.0.0.1:5173  (proxies /api to :8000)
```

## Workspaces

- **Overview** — analysis cards (yaw, speed, gear) + freshness
- **Live CAN** — latest-by-message table
- **Control** — enable Bench TX, inject host drive (yaw/speed) only — not a full synthetic vehicle

Safety-bypass style: inject only the signals under study. Full multi-ECU peer mesh is not the default.

## E2E tests

```bash
# starts backend :8010 + frontend :5174 automatically
npm run test:e2e
```
