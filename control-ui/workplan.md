# Control-UI (CUI) — Work Plan

**Status:** Streamlined Implementation Phase
**Last updated:** 2026-07-15

This work plan reflects the updated, streamlined approach to the Control-UI, bypassing the previous complex Vehicle Test Console (VTC) requirements.

## Phase 1: Backend Foundation (Python)
**Goal:** Establish the direct-to-hardware connection and WebSocket streaming server.

- [ ] Create `backend/requirements.txt` (`fastapi`, `uvicorn`, `python-can`, `websockets`).
- [ ] Initialize `VirtualTransportAdapter` (and later `CANalyst-II`) to open `High` (Ch0) and `Low` (Ch1) buses.
- [ ] Integrate generated `etrike_protocol.py` for raw-to-engineering value decoding.
- [ ] Implement SQLite recording sink to persist raw frames and decode errors (`cui.db`).
- [ ] Implement WebSocket endpoint `/api/stream` to push live state to the frontend.
- [ ] Implement `/api/inject` endpoint to receive commands, compute XOR/counters, and transmit.

## Phase 2: Frontend Foundation (React/Vite)
**Goal:** Port the static HTML into a reactive frontend connected to live telemetry.

- [ ] Scaffold `frontend/` using Vite + React + TypeScript.
- [ ] Port the CSS variables and layout grid from `control-ui/index.html`.
- [ ] Implement Zustand store to manage live CAN data incoming from WebSocket.
- [ ] Build the **Live CAN Table** component. Implement highlight-on-change and error (red) states.

## Phase 3: Hardware Control & Preview
**Goal:** Enable interactive manual testing and feedback visualization.

- [ ] Implement **Control Sidebar**. Add sliders for steering/brake and connect them to backend injection commands.
- [ ] Implement **Keyboard Teleoperation**. Bind WASD keys to drive commands (target: `0x300 HOST_DRIVE_CMD`).
- [ ] Implement **Tricycle Preview**. Port the Canvas/SVG tricycle visualizer. Wire the rendering logic strictly to `0x201 SES_STATUS` (steering) and `0x206 MTR_MOTOR_FBK` (speed) rather than UI input states.

## Phase 4: Bench Verification
**Goal:** Validate direct control against physical ECUs.

- [ ] Run backend against CANalyst-II. Ensure dual-channel reading operates smoothly.
- [ ] Ground hardware bypass pin to suppress timeout ESTOPs.
- [ ] Inject steering command (`0x169`); visually confirm Tricycle Preview updates correctly based on physical `0x201` feedback.
