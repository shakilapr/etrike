# Controller Tech Stack

This document defines the exact technology stack chosen for the CAN Controller, optimized for the absolute lowest latency, minimal lines of code, and a premium reactive user interface.

## 1. Frontend: React + TypeScript (The UI)
This stack provides a heavily typed, production-ready frontend that can scale into a desktop application.
- **React + Vite + TypeScript:** Industry standard for maintainable, fast interfaces. Strict mode ensures type safety when handling complex CAN data.
- **Zustand:** Ultra-fast, boilerplate-free state management to hold the live vehicle state.
- **TanStack Table:** The absolute best headless table library, perfect for rendering a live, scrollable CAN diagnostic log.
- **Tailwind CSS + shadcn/ui:** Guarantees a stunning, premium aesthetic out-of-the-box with highly customizable, accessible components.

## 2. Backend: Python + FastAPI (The Engine)
FastAPI provides a incredibly robust asynchronous foundation for streaming CAN data.
- **FastAPI:** Handles the WebSocket connections natively and asynchronously, ensuring zero blocking between the web clients and the CAN bus.
- **`python-can`:** Used to interface with the physical CANalyst-II USB adapter or the `virtual` software interface.
- **`cantools`:** Parses the DBC files to instantly decode binary payloads into human-readable JSON dictionaries.

## 3. Desktop Packaging (Future-Proofing)
- **Tauri:** By building with Vite and React, the web app can easily be wrapped in Tauri later. This allows the controller to be shipped as a native desktop application with a tiny memory footprint (unlike Electron).

## 4. Communication Flow
1. **Physical CAN Bus** <--(USB)--> **python-can** (Background Thread)
2. **Python Backend** <--(FastAPI Async WebSockets)--> **React UI**
3. **React UI** <--(Zustand)--> **shadcn/ui Dials & TanStack Tables**
