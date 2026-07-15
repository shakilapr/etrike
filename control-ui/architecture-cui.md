# Control-UI (CUI) — Architecture

**Status:** Streamlined Implementation
**Last updated:** 2026-07-15

## 1. Primary Objectives
- **Monitor:** Real-time visibility into all CAN traffic on the vehicle.
- **Control & Mimic:** Ability to inject specific frames to control actuators directly.
- **Simplicity:** Minimal runtime processes. Use direct-to-hardware testing without complex simulated profiles.

## 2. Supported Work Modes
- **Direct Hardware Control:** The tool connects directly to the High and Low buses via CANalyst-II. It relies on a physical hardware bypass pin to suppress timeout ESTOPs, eliminating the need for a complex "Synthetic Peer" simulation engine in software.

## 3. Technology Stack

### Frontend
- **React + Vite + TypeScript:** For a maintainable, fast interface with reactive state.
- **Zustand:** Ultra-fast state management for live CAN traffic.
- **UI Components:** Re-using the premium CSS layout and SVG/Canvas preview from the original `index.html` prototype.

### Backend
- **Python + FastAPI:** Provides the asynchronous WebSocket foundation.
- **python-can:** Standard transport connecting directly to the CANalyst-II.
- **SQLite:** A lightweight, single-file database (`cui.db`) for persisting raw frame logs and anomalies, allowing post-test debugging.
- **Generated YAML codecs:** Reuses existing `protocol/codecs/python` generated from YAML to decode/encode payloads.

## 4. Feature Requirements

### Live CAN Workspace
- A high-performance table showing the latest decoded messages for both buses.
- **Highlighting:** Values flash briefly upon update. Erroneous messages (e.g., checksum mismatch, DLC length error, out-of-bounds) highlight in red and are logged to SQLite.

### Control Sidebar & Keyboard Teleoperation
- **Sidebar:** Sliders and toggles for sending specific manual commands (e.g., `0x169` steering angle, `0x7B9` brake pressure).
- **WASD Teleoperation:** A global keyboard listener that captures input to drive the vehicle (targeting `0x300 HOST_DRIVE_CMD` or direct actuator commands).
- **Dynamic Checksums:** The backend automatically computes and appends rolling counters and XOR checksums dynamically for injected actuator frames.

### Tricycle Preview
- A visual 2D top-down representation of the tricycle.
- **Feedback-Driven:** The graphic's state (steering angle, wheel rotation speed) is wired strictly to incoming CAN telemetry (`0x201 SES_STATUS`, `0x206 MTR_MOTOR_FBK`). It does not respond directly to UI input, guaranteeing a true reflection of physical hardware response.

## 5. Excluded Features (Out of Scope)
Based on project needs, the following "overkill" features from previous VTC iterations are permanently removed:
1. **Synthetic Peer Engine:** No fake heartbeats are broadcast; hardware bypass pins handle timeout suppression.
2. **Formal Lease & Profile Management:** No explicit "Test Profiles" or "Resource Leases". The UI permits direct, unrestricted transmission.
3. **Formal Evidence Quality Gates:** No strict "Complete/Degraded" test executor grading. Testing relies on live streaming, SQLite logging, and manual verification.
