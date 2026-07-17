# Control Toolkit: Phase 1 Implementation Summary

## Overview
This document summarizes the transition of the Control Toolkit from a theoretical design into a functional, runnable Phase 1 implementation. The development session resolved major architecture-to-code gaps, stabilized the backend infrastructure, and polished the frontend application.

## 1. Backend Stabilization & Concurrency
The initial skeleton backend was unable to handle real-time CAN traffic efficiently, resulting in frequent 502 Bad Gateway and `Offline` UI states.
* **WebSocket Race Conditions**: A critical flaw in FastAPI/Starlette emerged under EventBus load—attempting to `asyncio.wait()` on WebSocket endpoints concurrently caused stream handler crashes. This was fixed by separating client receive and EventBus send into two distinct, long-lived loops, locking `send_json`, and preventing sends to closed sockets.
* **Clean Shutdowns**: The `lifecycle.py` and `canalyst.py` modules were reworked to prevent double-tracking of router tasks and to ensure that CAN hardware buses only disconnect after the background RX thread has fully exited.

## 2. Teleoperation & Intent Sequencing
Switching between different control surfaces (like the Drive Console and the standard Control Keyboard) caused `409 Conflict` errors due to overlapping TX leases.
* **Lease Management**: Handoffs were smoothed out by strictly enforcing `control.release`. When the user leaves the Drive tab, it now explicitly disarms and clears the TX ownership lease, allowing other injection panels to safely claim the bus.
* **Signal Shaping**: Fixed internal logical flaws in `control_intent.py` regarding reverse vs. forward gear shaping.

## 3. UI Polish & Experience
* **Layout Shifts Resolved**: UI components (buttons, keycaps) were physically expanding upon click/selection due to missing state constraints. Hard-coded padding, heights, and borders were enforced across all interactive pseudo-classes.
* **Data Presentation**: The Overview UI was cluttered with redundant tags (e.g., `enabled · enabled`, `lost · lost`). These were collapsed into single, meaningful state pills.
* **Computer vs. Real Mode**: A top-bar toggle was introduced to instantly switch the backend session between `pure_software` and `bench_test` hardware, removing the need to dig into settings.

## 4. Protocol Codecs & Typescript Alignment
The YAML protocol acts as the static wire dictionary, but issues existed in how it was digested by the UI:
* Duplicate `SIGNAL_DOCS` keys were breaking the `tsc` compiler and were removed.
* Opaque vendor frames like `0x201 SES_STATUS` and SEB frames were showing up as empty tables in the Dictionary. Codec-aligned field maps were built, allowing the UI to decode and display physical units and enums for low-level brake and steer buses.

## 5. Testing & Developer Tooling
* **E2E Playwright Audits**: A live click-through audit (`live-click-audit.spec.ts`) was added to continuously assert that the UI and backend remain properly wired.
* **Software-Only Testing**: A clear `testing-guide.md` was established, explaining that in Computer mode, the toolkit transmits high-level `0x300 HOST_DRIVE` frames. However, because there is no RT gateway running in the backend, it will *not* automatically fan out into low-level `0x204` or `0x169` frames. Test scripts (`software_only_recipe_qa.py`) were built to explicitly test these API paths.
* **Start Scripts**: Dedicated `start-api.ps1` and `start-ui.ps1` scripts, explicitly binding to `127.0.0.1`, were added to resolve cross-origin proxy headaches.
