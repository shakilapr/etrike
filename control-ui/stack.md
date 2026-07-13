# Controller Tech Stack

This document defines the technology stack for the CAN Controller. It prioritizes maintainability, responsive live updates, and a clear engineering UI without treating latency, line count, or visual quality as guarantees from a framework.

## 1. Frontend: React + TypeScript (The UI)
This stack provides a heavily typed, production-ready frontend that can scale into a desktop application.
- **React + Vite + TypeScript:** Industry-standard tooling for maintainable, fast interfaces. TypeScript provides static type checking; React Strict Mode helps expose unsafe lifecycle behavior during development.
- **Zustand:** Ultra-fast, boilerplate-free state management to hold the live vehicle state.
- **TanStack Table:** Headless table composition for the latest-message view. The chronological CAN monitor also requires a virtualized row renderer.
- **Tailwind CSS + shadcn/ui:** Utility styling and accessible component primitives. Application design and accessibility review remain required.

## 2. Backend: Python + FastAPI (The Engine)
FastAPI provides an asynchronous HTTP and WebSocket foundation for streaming CAN data.
- **FastAPI:** Handles HTTP and WebSocket connections. Blocking CAN-driver and disk work must remain in dedicated workers so it cannot block API handling.
- **`python-can`:** Used to interface with the physical CANalyst-II USB adapter or the `virtual` software interface.
- **Generated YAML protocol runtime:** Generated codecs, validators, and UI metadata decode and encode application traffic. `cantools` and DBC files are optional development interoperability tools, not runtime dependencies.

## 3. Desktop Packaging (Future-Proofing)
- **Tauri:** A possible later packaging option. Python sidecar lifecycle, USB-driver access, signing, and distribution must be evaluated before adoption.

## 4. Communication Flow
1. **Physical CAN Bus** <--(USB)--> **python-can** (Background Thread)
2. **Python Backend** <--(FastAPI REST/WebSockets)--> **React, LLM tools, CI, optional CLI**
3. **React UI** <--(generated TypeScript API client + Zustand)--> **shadcn/ui Dials & TanStack Tables**

## 5. Shared API Clients

- **Pydantic + OpenAPI:** Pydantic models are the single request/response definition. FastAPI publishes OpenAPI for generated React clients and as the source contract for optional LLM/CLI translations. OpenAPI describes the API; it does not execute calls.
- **Direct Claude Code/Python:** Claude Code and tests may use a small HTTPX client or permitted terminal HTTP calls directly against FastAPI; no MCP layer is required.
- **Optional LLM tool adapter:** Anthropic API applications can expose selected API operations as client tools. Claude Desktop/Claude.ai may use a small MCP adapter. Either adapter only translates to REST/WebSocket and contains no domain or CAN logic.
- **Optional thin CLI:** Typer + HTTPX may provide terminal convenience over the same API. It is not a separate backend or service owner.
- **Headless browser:** Playwright exercises React against the same backend with deterministic virtual fixtures and captures traces/screenshots on failure.
- **Shared streaming:** React, LLM, and CLI clients use the same versioned WebSocket subscription protocol and independent bounded client queues.

The detailed contract is in `control-ui-api.md`.
