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

- **Overview** — status cards for key messages + freshness
- **Live CAN** — latest-by-message table
- **Control** — enable Bench TX, synthetic peers, inject Host drive, Stop All
