# E-Trike Debug Tool — Advanced Architecture Blueprint

**Version:** 1.0.0 (Optimal Architecture)

This document serves as the absolute engineering blueprint for the `debug-tool` platform. It outlines a high-performance, deterministic, zero-allocation architecture designed to function as both a human-driven CAN inspection UI and a headless Software-In-the-Loop (SIL) Rig for AI autonomous development.

---

## 1. Core Architectural Principles

1. **Single Source of Truth**: `can_high.yaml` and `can_low.yaml` govern the entire system. No hardcoded CAN IDs (`0x300`), static dictionaries, or manual bit-shifting are permitted anywhere in the UI or backend.
2. **Zero-Allocation Frontend**: The UI is a stateless rendering engine. It relies on Binary WebSockets, Web Workers, and `SharedArrayBuffer` ring-buffers to process 30,000+ FPS without Javascript Garbage Collection (GC) pauses.
3. **Native C++ Firmware Integration**: We do not rewrite first-party firmware logic (RT, SYS, MTR) into TypeScript. We compile the real C++ code natively and run it via IPC.
4. **Deterministic Virtual Time**: The simulation does not use the host computer's system clock. Time is explicitly stepped via IPC, ensuring bit-perfect reproducibility for automated tests and accelerated AI training.

---

## 2. Backend Simulation Engine

The backend (Node.js/Fastify) acts as a high-speed router and orchestrator. It manages the flow of CAN frames between the physical Serial bridges, the Web UI, and the Native C++ IPC binaries.

### 2.1 Native IPC: Strict Controller vs. Plant Separation
To guarantee that the simulation behaves exactly like the physical E-Trike, the architecture enforces a strict decoupling between the **Controller (Firmware)** and the **Plant (Physics Environment)**. Mashing them into the same routine prevents independent validation.
- **The Controller (Firmware Binary)**: Fastify spawns the compiled production firmware (`firmware-native`). This binary contains *only* the control logic and safety monitors.
- **The Plant (Physics Binary/Model)**: Fastify separately manages the physics environment (`plant-model`). It takes the actuator commands from the firmware, calculates the real-world kinematics (inertia, friction, gravity), and generates the resulting sensor feedback.
- **Zero-Copy Shared Memory IPC**: JSON over stdin/stdout introduces unacceptable allocation, UTF-8 parsing, and backpressure bottlenecks. Instead, the Node.js backend routes data between the Controller and the Plant using **POSIX Shared Memory (mmap)** or highly optimized raw binary pipes. This ensures zero-allocation, microsecond-latency communication.

### 2.2 Third-Party Behavioral Models
Since we do not have access to vendor source code, third-party actuators (EPS-C, SEB) are the *only* units mimicked in TypeScript. They are implemented as simple state machines within `backend/src/sim/ecus/` and rely on `encodePayload()` to serialize data dynamically.

### 2.3 Pluggable Clock Semantics (Time Domains)
A critical architectural constraint is that Physical, SIL, Replay, and HIL execution modes use fundamentally incompatible clock semantics. A single monolithic execution model fails because `Date.now()` is meaningless in a replay or virtual simulation. 
To resolve this, the backend architecture relies on a **Pluggable Clock Provider** injected into the `SimulationEngine`:
- **Physical Mode (Wall Clock)**: Uses standard Node.js `performance.now()`. Driven by real hardware serial ports.
- **SIL Mode (Virtual Clock)**: The backend owns time. It steps the C++ IPC binary explicitly via `{"type":"tick"}` commands, enabling pause, determinism, and accelerated AI training.
- **Replay Mode (Log Clock)**: Driven by timestamps stored in SQLite or Vector `.asc` files. Time progresses strictly based on the relative deltas between logged frames.
- **HIL Mode (CANalyzer Clock)**: Time is enslaved to the Vector CANalyzer COM measurement clock to guarantee synchronization with the physical VN1630 hardware.
All TS models, Ring Buffers, and Web Workers MUST query the injected `ClockProvider.now()` instead of system time.

---

## 3. High-Performance Transport Layer

MQTT (`Aedes`) is fully deprecated due to overhead and dependency bloat. The system uses strict WebSockets and ArrayBuffers.

### 3.1 Binary WebSockets (Strict 16-Byte Alignment)
Instead of heavy `JSON.stringify` overhead, CAN frames are packed into dense, strictly 16-byte aligned binary arrays to prevent unaligned memory penalties during Web Worker chunking:
```c
struct __attribute__((packed)) WsFrame {
    uint32_t timestamp_ms; // 4 bytes
    uint16_t bus_and_id;   // 2 bytes (bit 15: bus, bits 0-11: CAN ID)
    uint8_t  dlc;          // 1 byte
    uint8_t  flags;        // 1 byte (e.g., 0x01: injected, 0x02: error frame)
    uint8_t  data[8];      // 8 bytes
}; // Total: exactly 16 bytes
```

### 3.2 Data Persistence & Automotive Exports
Every frame is piped to an asynchronous Worker Thread that executes Chunked Batching (e.g., flushing 5,000 frames per 500ms into an in-memory SQLite buffer). 
To prevent vendor lock-in, the backend API (`/api/export`) converts SQLite sessions into standard Vector `.asc` or `.blf` formats, allowing engineers to analyze captures in CANalyzer or Wireshark.

---

## 4. Zero-Allocation Frontend (Svelte 5)

The frontend is built to handle maximum CAN bus saturation without dropping frames or staggering.

### 4.1 Web Worker Static Decoder (AOT Generation)
Runtime dynamic decoding (looping through a JSON schema, executing `BigInt` bit-shifts, and allocating dictionary objects) completely destroys zero-allocation objectives at 30,000 FPS. Instead, the architecture mandates **Ahead-of-Time (AOT) Code Generation**. The `generate_code.py` toolchain generates a hardcoded, static TypeScript decoder (`decoder.gen.ts`). When a 16-byte frame arrives, the Web Worker executes a single `switch(id)` statement and uses direct bitwise masks to extract values, writing them straight into fixed memory addresses. Zero `BigInt` usage, zero object instantiation, zero garbage collection.

### 4.2 SharedArrayBuffer & Atomics (Lock-Free Synchronization)
A raw `SharedArrayBuffer` lacks synchronization; a concurrent read/write between the Main Thread and Web Worker can result in "torn" values or inconsistent signal sets. To resolve this:
- **Atomics & SeqLocks**: The Web Worker utilizes `Atomics.store` and a Sequence Lock (seqlock) pattern when updating the buffer. It increments an atomic sequence counter (odd = writing), writes the signals, and increments again (even = done). 
- **Torn-Read Prevention**: The Svelte 5 UI reads the sequence counter before and after fetching data. If the counter is odd or has changed, it retries, guaranteeing memory consistency without blocking the writer.
- **Svelte Runes**: The UI utilizes `$state` and `$derived` runes to pull this guaranteed-consistent data directly from memory for zero-overhead, 60 FPS reactivity.

---

## 5. Headless SIL & AI Development Integration

This architecture scales beyond a visual debug tool into a headless Software-In-the-Loop (SIL) training rig.

### 5.1 Python RL Interface
The Fastify backend exposes a dedicated gRPC/WebSocket endpoint for Python (PyTorch/TensorFlow). An OpenAI Gym agent can connect to the backend, receive physical sensor feedback, and send steering/throttle commands natively.

### 5.2 Scenario Fuzzing & The Optimal CLI / MCP Integration
Instead of a slow, Node-based CLI (`npm run cli`), the architecture employs a lightning-fast, AI-optimized tooling suite:
- **Model Context Protocol (MCP) Server (Sandboxed Omniscience)**: The backend natively exposes an MCP server to AI assistants. While the AI has total visibility into the system, **physical injection is strictly sandboxed**:
  - **Execution Mode Enforcement**: By default, AI-driven CAN injection is *only* permitted in `SIL` (Virtual) mode. If the engine is in `PHYSICAL` or `HIL` mode, the MCP server outright rejects all drive/steer injection requests to prevent physical harm from AI hallucinations.
  - **Hardware Deadman Override**: Physical injection by the AI can only be unlocked via a physical hardware deadman switch (e.g., a physical button held by an engineer on the test bench).
  - **Firmware-in-the-Loop (FIL) Safety**: A static YAML dictionary cannot describe dynamic safety behaviors (e.g., preventing a 40-degree steer at 30km/h). Therefore, the MCP Sandbox does not rely on YAML limits. All AI-injected commands are routed *through* the native C++ `firmware-native` binary. The production firmware's internal `safety_monitor.cpp` serves as the ultimate arbiter, naturally rejecting or clamping unsafe AI commands exactly as it would on the real vehicle.

### 5.4 Vector CANalyzer Bridge (Hardware-in-the-Loop)
To bridge the gap between the virtual Node.js environment and physical hardware testing, the Fastify backend includes a **Vector CANalyzer COM Bridge**:
- **Windows COM Integration**: The debug tool uses Windows COM (`win32com`) to remotely control a running instance of Vector CANalyzer. 
- **LLM-Driven HIL Testing**: Because the LLM has total access to the debug tool via MCP, the LLM gains the unprecedented ability to command the physical Vector hardware (VN1630). The AI can instruct the debug tool to trigger CANalyzer measurements, inject physical CAN signals onto the real E-Trike bus, and read physical hardware telemetry—giving the LLM complete, scriptable control over industry-standard HIL test benches.

---

## 6. Smart Logging: Lossless Capture vs. Filtered Application Logs
Because the simulation and physical bus operate at extreme frequencies (e.g., 30,000 CAN frames/sec, 100Hz IPC ticks), the logging architecture separates forensic data capture from human-readable application logs to prevent noise without destroying evidence.
- **Lossless Raw CAN Capture**: Forensic information is sacred. The debug tool includes a dedicated, low-level asynchronous thread that dumps the raw 16-byte binary WebSocket frames straight to disk (or memory-mapped SQLite blob) with zero deadband filtering. This ensures that every single microsecond oscillation and exact transmission frequency is preserved for regulatory analysis or Vector `.asc` export.
- **Filtered Application Logger**: For the human-readable text logs (e.g., `pino`), writing every CAN frame would create terabytes of noise. The Application Logger applies Delta Deadband filtering. High-frequency signals only trigger an `INFO` text log if they cross a configured threshold, reducing log spam for engineers debugging AI state transitions.
- **In-Memory "Flight Recorder"**: `TRACE` level application logs (like IPC pipeline heartbeats) are kept in a rolling 10,000-line in-memory ring buffer. If a `FAULT` occurs, the backend instantly dumps this text buffer to disk to provide context alongside the lossless raw CAN data.
- **Statically Compiled CLI**: For CI/CD and shell scripting, we will provide a lightweight, statically compiled binary (e.g., Go or Rust) that executes in <5ms (avoiding the Node.js V8 boot overhead). 
- **Machine-Readable Outputs**: The CLI natively supports `--json` flags on every command, ensuring that automated scripts and AI tools receive structured, deterministic responses rather than unstructured terminal text.
- **Example Usage**: `etrike-cli inject HOST_DRIVE_CMD --speed=500 --json`

### 5.3 Continuous Feedback & Progressive Updates
To support advanced AI training and dynamic debugging, the test framework is not a simple "fire-and-forget" binary outcome. It supports **Progressive Updates** while tests are actively running:
- **Streaming Telemetry via MCP**: AI tools can subscribe to continuous telemetry streams via the MCP server (using JSON-RPC notifications) rather than polling. This allows an AI agent to monitor a scenario (like a steering maneuver) in real-time and react to anomalies instantly.
- **Hot-Reloading & Parameter Tuning**: The AI can issue parameter updates (e.g., `etrike-cli set-param --kp=1.5`) while the Virtual Clock is actively ticking. The Fastify backend forwards these progressive updates down the IPC pipe to the C++ binary's HAL, dynamically altering the physics or control logic without requiring a full simulation reboot. This enables rapid, continuous feedback loops for Reinforcement Learning (RL) agents or automated hyperparameter tuning.
