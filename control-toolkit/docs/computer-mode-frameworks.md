# Computer mode and framework codebases

This document explains how **Computer mode** (Pure Software / virtual dual CAN) works in the Control Toolkit, and which **framework packages** it reuses instead of re-implementing.

---

## 1. What “Computer mode” is

| UI label | Session profile | Destination | Transport |
|----------|-----------------|-------------|-----------|
| **Computer (virtual)** | `pure_software` | `virtual` | In-process dual virtual CAN (High + Low) |
| **Real (CANalyst-II)** | `bench_test` / `full_vehicle` | `physical` | USB CANalyst-II CH0=High, CH1=Low @ 500 kbit/s |

Computer mode is the **default** profile (`ToolkitConfig.default_profile = pure_software`).

Goals:

- Run the **same backend** and **same protocol codecs** as Real mode.
- No USB adapter, no silent fallback from physical to virtual when Real is selected.
- Suitable for UI development, inject/keyboard, dictionary, dual-bus API QA, and CI.

It is **not** a full vehicle dynamics simulator. Complex ECU state machines remain on firmware / Future Work.

---

## 2. Monorepo “framework” packages Computer mode uses

Computer mode does **not** invent a private protocol. It sits on shared repo trees:

```
etrike/
├── protocol/                 # YAML contracts + generated codecs (wire SoT)
│   ├── contracts/*.yaml
│   ├── generated/python/     # etrike_protocol.METADATA, hashes
│   └── codecs/python/        # encode / decode runtime
├── control-toolkit/          # This product (API + UI + virtual/physical adapters)
│   ├── backend/control_toolkit/
│   └── frontend/
├── control-ui/               # Sibling product (do not bind both to :5173)
├── debug-tool/               # Dictionary layout inspiration / reference
└── (rt-esp32, sys-esp32, …)  # Firmware that compiles against same YAML
```

### 2.1 Protocol package (primary shared framework)

| Piece | Role in Computer mode |
|-------|------------------------|
| `protocol/contracts/*.yaml` | Message layouts, enums, instances (High/Low), periods |
| `protocol/generated/python/etrike_protocol.py` | `METADATA` catalog, `WIRE_HASH` / `SEMANTIC_HASH` / `NETWORK_HASH` |
| `protocol/codecs/python` | `encode()` / `decode()` used for every inject and RX path |

**Seam:** only `control_toolkit/protocol_bridge.py` imports the protocol package. It adds the monorepo root to `sys.path` and re-exports encode/decode/catalog.

```text
UI / API → TxGate / Router → encode_message / decode
                              └── protocol_bridge → protocol.codecs + generated
```

Firmware (RT/SYS) is generated from the **same YAML**, so Computer-mode injects are wire-compatible with Real mode when hashes match.

### 2.2 Control Toolkit backend (application framework)

| Layer | Computer-mode behavior |
|-------|-------------------------|
| **FastAPI** (`main.py`, `api/*`) | REST + WebSocket contract (status, sessions, control, inject, logs, …) |
| **Lifecycle** | On `pure_software`: `open_virtual_transport()` |
| **VirtualTransportAdapter** | `python-can` interface `virtual`, two channels High + Low |
| **Router / decoder / validator** | Same pipeline as physical RX |
| **TxGate** | Profile + **Bench TX** + ownership + encode before TX |
| **Scheduler** | Periodic re-encode (Host 10 ms, SES/SEB 20 ms, …) |
| **ControlIntentService** | Keyboard/intent → shaped `HOST_DRIVE_CMD`; Low direct actuators |
| **SessionManager** | Profile, destination=`virtual`, Bench TX gate |
| **EventBus + stream** | WebSocket `state` + `heartbeat` to UI |

### 2.3 Transport frameworks

| Library | Computer | Real |
|---------|----------|------|
| **python-can** `virtual` | Yes — dual named channels in-process | No |
| **python-can** + CANalyst-II driver | No | Yes (CH0 High / CH1 Low) |

Virtual buses are **shared by channel name within the process**, so inject TX is observed as RX on the same adapter (no HW echo required). Capability record: no HW timestamps, no TX echo.

### 2.4 Frontend frameworks (UI only)

| Package | Role |
|---------|------|
| React + Vite | Shell, Control, Drive, Dictionary, Live CAN |
| Zustand | Status / messages / stream quality |
| Playwright | e2e against Pure Software by default |

UI never opens CAN directly. It calls the **same API** as scripts and CI (`/api/v1/*`).

---

## 3. Runtime path in Computer mode

```
┌─────────────────────────────────────────────────────────────┐
│ Browser (React)  :5173                                      │
│  REST + WS  ──proxy──►  FastAPI  :8001                      │
└────────────────────────────┬────────────────────────────────┘
                             │
              Lifecycle (profile=pure_software)
                             │
              VirtualTransportAdapter (python-can)
                    ┌────────┴────────┐
                    │ high virtual    │ low virtual
                    └────────┬────────┘
                             │ RX queue (raw frames only)
                      Router → decode (protocol) → LatestStore
                             │
              WebSocket batches type=state + type=heartbeat
```

**TX path (Computer):**

1. UI/API enables session + **Bench TX** (safety gate).
2. Control / inject / scheduler builds engineering values.
3. `TxGate` → `encode_message` (protocol) → virtual bus emit.
4. Same process receives frame as RX → decode → Live CAN / stream.

**High vs Low (same as Real):**

| Logical bus | Computer map | Typical messages |
|-------------|--------------|------------------|
| High | `virtual:high` | `HOST_DRIVE_CMD 0x300`, HMI, Host/RT high |
| Low | `virtual:low` | `RT_DRIVE_CMD 0x204`, `VCU_SES_REQ 0x169`, `VCU_SEB_REQ 0x7B9` |

---

## 4. How Computer mode is selected

### Backend

- Default: `Profile.PURE_SOFTWARE` in `config.py`.
- Env: `CTK_PROFILE=pure_software` (via `ToolkitConfig.from_env`).
- Session create: `POST /api/v1/sessions` with `"profile": "pure_software"`.
- Settings UI: **Computer** → profile `pure_software`.
- `CTK_TRANSPORT=canalyst` forces physical and **fails** if adapter missing (no silent virtual).

### Frontend

- Settings transport toggle **Computer** starts/restarts a Pure Software session.
- Top bar: **Computer · Virtual** when profile is not bench/full vehicle.

---

## 5. What Computer mode deliberately does *not* use

| Not used | Why |
|----------|-----|
| CANalyst-II / USB | No hardware in Pure Software |
| Full vehicle physics engine | Deferred; optional canvas sim is UI-local |
| Separate “UI protocol” | One YAML-generated contract only |
| DBC at runtime | Optional export for third-party tools only |
| Silent physical→virtual fallback | Architecture rule: Real refuses without adapter |

---

## 6. Related framework code (siblings)

| Codebase | Relationship to Computer mode |
|----------|-------------------------------|
| **`protocol/`** | Required; codecs + catalog for virtual TX/RX |
| **`control-toolkit/`** | Owns virtual adapter + API + UI |
| **`debug-tool/`** | Reference UX for dictionary bit grids (not imported at runtime) |
| **`control-ui/`** | Separate app; **do not** share port 5173 with toolkit |
| **Firmware trees** (`rt-*`, `sys-*`, …) | Same YAML contracts; not linked at Python runtime in Computer mode |

---

## 7. How to run Computer mode locally

```powershell
# Terminal 1 — backend (opens virtual High+Low by default)
cd e:\work\etrike\control-toolkit\backend
python -m uvicorn control_toolkit.main:app --host 127.0.0.1 --port 8001

# Terminal 2 — UI (proxy /api → 8001)
cd e:\work\etrike\control-toolkit\frontend
$env:CTK_E2E_API = "http://127.0.0.1:8001"
npm run dev -- --host 127.0.0.1 --port 5173 --strictPort
```

Then in UI: **Settings → Computer**, or Control **Turn Bench TX on** (creates Pure Software session if needed).

**API smoke (no UI):**

```powershell
cd e:\work\etrike\control-toolkit\backend
python scripts/dual_bus_api_qa.py --base http://127.0.0.1:8001
```

---

## 8. Heartbeat: backend ↔ frontend (not vehicle)

Computer mode uses the same stream as Real:

| Message | Default rate | Purpose |
|---------|--------------|---------|
| WS `type=state` | ~25 Hz (`latest_state_batch_hz`) | Latest decoded messages + session |
| WS `type=heartbeat` | every 250 ms (`stream_heartbeat_ms`) | Keep UI stream “Live” with no CAN traffic |
| HTTP `GET /status` | UI poll ~2 s | Recover if WebSocket is flaky |

This is **browser↔API** liveness, separate from ECU heartbeats on the virtual buses.

---

## 9. Quick mental model

> **Computer mode = Pure Software profile + python-can virtual High/Low + shared `protocol` encode/decode + full Control Toolkit services.**  
> Same wire contract as firmware and Real mode; only the transport adapter changes.

For physical CANalyst mapping and Bench/Full Vehicle behavior, see `architecture-control-toolkit.md` and Settings → Real (CANalyst-II).
