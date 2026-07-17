# Changelog: Control Toolkit

All notable changes to the Control Toolkit project will be documented in this file.

## [Unreleased] - Phase 1 Implementation & System Stabilization

This release covers the complete implementation of Phase 1 of the Control Toolkit, bringing the system from an empty backend skeleton to a functional, stable, and testable bench engineering application.

### Major Problems Faced & Solved

#### Backend Stability & Concurrency
* **Problem**: The Vite proxy frequently dropped connections, resulting in `Offline` and `Lost` errors in the UI. Under heavy EventBus traffic, FastAPI/Starlette threw "Concurrent call to receive() is not allowed" errors, crashing the WebSocket stream.
* **Solution**: Completely refactored the WebSocket stream. Split client receive and EventBus send into separate long-lived pumps. Serialized `send_json` under a lock and safely skipped sending after close to resolve ASGI race conditions.
* **Problem**: Tasks were not shutting down cleanly, leaving zombie threads.
* **Solution**: Fixed `lifecycle.py` to correctly track and cancel router tasks. Modified `canalyst.py` to ensure the hardware bus only shuts down after the RX thread safely exits.

#### Control Intent & Hardware Handoff
* **Problem**: Handing off control between the UI's Control keyboard and the Drive console resulted in `409 Conflict` errors due to stale intent sequences.
* **Solution**: Implemented proper lease releasing. `control.release` now cleanly clears TX ownership leases. Drive arming and Control keyboard now claim the bus correctly, and leaving the Drive tab automatically disarms the system.
* **Problem**: The drive gear shaping logic (reverse vs forward) was flawed.
* **Solution**: Fixed gear shaping inside `control_intent.py`.

#### UI Bugs & Polish
* **Problem**: UI buttons (keycaps, segments, danger controls) were physically growing in size when clicked or selected, shifting the layout.
* **Solution**: Locked padding, height, border-width, and font-weight across rest/hover/active/selected states.
* **Problem**: The Overview workspace displayed duplicate status text (e.g., `enabled · enabled`, `lost · lost`).
* **Solution**: Collapsed identical Req/Conf pairs into single pills and consolidated CAN health into a single stream-health word.
* **Problem**: Switching between simulated and physical buses was tedious.
* **Solution**: Added a dual-color segmented toggle (Computer/Real) in the top bar to instantly restart the session in the desired mode.

#### Protocol Dictionary & Codecs
* **Problem**: The TypeScript dictionary compilation broke due to duplicate `SIGNAL_DOCS` keys. Furthermore, SES/SEB opaque fields were missing from the dictionary, leaving UI tables empty.
* **Solution**: Removed duplicate keys fixing `tsc`. Added codec-aligned field maps for SES/SEB (e.g., `0x201 SES_STATUS`), wiring them into the dictionary and bit-grid, and enriched live decoding with enums and physical units.

### Architecture & Documentation Realignment
* **Design vs Implementation Gap**: Previously, the toolkit had ~2.7k lines of architecture documentation but no backend logic. The `workplan.md` was restructured to focus strictly on Phases 0–7 (Core), deferring speculative features (Tauri, LLM, 3D vehicle preview) to Future Work.
* **YAML Protocol Misconception**: Clarified in `architecture-control-toolkit.md` that YAML definitions provide static wire dictionaries, but hand-written application code is still required for Bench TX, routing, and UI presentation.
* **API Dictionary**: Created `docs/api-dictionary.md` mapping all REST and WebSocket routes.
* **Testing Guide**: Authored `docs/testing-guide.md` explicitly defining "software-only" mode and documenting how high-level Host commands do not automatically expand into low-level frames without an RT gateway.
* **Developer Experience**: Added `scripts/start-api.ps1` and `start-ui.ps1`. Wired `toolkit:api` and `toolkit:ui` into the monorepo `package.json`. Configured Vite strictly to `127.0.0.1` via `CTK_E2E_API`.

### Remaining UI/API Wiring Gaps Identified (Via Playwright Audit)
A live Playwright click-audit (`frontend/e2e/live-click-audit.spec.ts`) was implemented, validating the entire UI against the backend API. It highlighted the following remaining missing UI elements:
* **Bench**: Missing action buttons to start/stop synthetic peers.
* **Diagnostics**: Missing a button to export recordings, and capturing on a quiet bus results in 0 frames.
* **Settings**: Lease management and free-form CAN injection APIs exist but lack UI wiring.
