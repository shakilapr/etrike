# Control Toolkit — Desktop Application Architecture & Packaging Specifications

This document specifies the technical architecture, installation standards, multi-OS support, and security/anti-reverse engineering strategies for packaging **Control Toolkit** into a native desktop application for Windows and Linux.

---

## 1. Overview & Single Source of Truth

Control Toolkit consists of:
- **Frontend**: React / Vite single-page application (`control-toolkit/frontend`)
- **Backend**: FastAPI / Python REST & WebSocket application (`control-toolkit/backend`) with CANalyst-II USB drivers (`python-can`, `pyusb`).

### Single Source of Truth Architecture
The `control-toolkit` codebase remains the **single source of truth** for both Web and Desktop applications:
- **No Code Duplication**: Electron wraps the existing React frontend and FastAPI backend without requiring separate repositories or separate UI codebases.
- **Dual Execution Modes**:
  - **Web Development Mode**: `npm run toolkit:ui` + `npm run toolkit:api` (runs in browser).
  - **Desktop Application Mode**: `npm run toolkit:electron` (runs inside native Electron window shell).
- **Synchronized Feature Delivery**: Any UI modification or backend feature added to `control-toolkit` immediately updates both Web and Desktop distributions.

---

## 2. Desktop Application Architecture

```
+-----------------------------------------------------------------------------------+
|                            ELECTRON DESKTOP SHELL                                 |
|                                                                                   |
|  +-------------------------------+             +-------------------------------+  |
|  |     Electron Main Process     |             |      BrowserWindow (UI)       |  |
|  |     (frontend/electron)       |             |   (React / Vite Renderer)     |  |
|  +---------------+---------------+             +---------------+---------------+  |
|                  |                                             |                  |
|        Spawns Subprocess                               REST & WebSockets          |
|                  |                                     (http://127.0.0.1:8001)    |
|                  v                                             v                  |
|  +-----------------------------------------------------------------------------+  |
|  |                 FastAPI / Python Backend Subprocess                         |  |
|  |   (Compiled sidecar: control_toolkit_backend.exe / control_toolkit_backend) |  |
|  |       - CANalyst-II USB / WinUSB / libusb / SocketCAN Drivers               |  |
|  |       - SQLite Database & Real-time Telemetry Engine                        |  |
|  +-----------------------------------------------------------------------------+  |
+-----------------------------------------------------------------------------------+
```

### Process Lifecycle
1. **Startup**: When launched, the Electron main process spawns the compiled Python backend sidecar binary.
2. **Health Check**: Electron polls `http://127.0.0.1:8001/api/v1/status` until the backend server responds.
3. **UI Render**: Electron opens the main window loading the React frontend.
4. **Shutdown**: When the user closes the desktop application, Electron sends SIGTERM/SIGINT signals to cleanly terminate the Python backend process and release hardware USB resources.

---

## 3. Installation Standards & Multi-OS Support

Control Toolkit supports both **Windows** and **Linux** operating systems.

### Windows Packaging Standards
- **NSIS Installer (`ControlToolkit-Setup-1.0.0.exe`)**:
  - Installs to `%LOCALAPPDATA%\Programs\ControlToolkit` or `C:\Program Files\ControlToolkit`.
  - Creates Start Menu shortcuts, Desktop icon, and Add/Remove Programs uninstaller registry entry.
  - Supports silent corporate/lab bench deployment via `/S` command flag.
- **Portable Executable (`ControlToolkit-1.0.0.exe`)**:
  - Single standalone file that runs directly without installation (ideal for field testing and portable lab USB drives).

### Linux Packaging Standards
- **AppImage (`ControlToolkit-1.0.0.AppImage`)**:
  - Universal single-file Linux executable compatible across Ubuntu, Debian, Fedora, Arch, and RHEL without root/sudo installation.
- **Debian Package (`control-toolkit_1.0.0_amd64.deb`)**:
  - Native Linux package for system package managers (`apt`, `dpkg`).

### Cross-Platform Hardware Compatibility
- **Windows**: Interfaces with CANalyst-II USB via WinUSB / userspace USB drivers.
- **Linux**: Interfaces via `libusb-1.0` and native Linux SocketCAN (`can0`, `vcan0`).

---

## 4. Anti-Reverse Engineering & Intellectual Property Protection

To protect CAN protocol definitions, proprietary signal routing, and control logic from decompilation or reverse engineering:

### Backend Protection (Python)
1. **Nuitka / Cython Machine-Code Compilation**:
   - Python backend source files are translated into C code and compiled using GCC/MSVC into native machine-code binaries (`.exe` on Windows, `.so` shared objects on Linux).
   - Machine-code binaries cannot be decompiled back into Python source code using standard Python decompilers (e.g., `pycdc`, `uncompyle6`).
2. **Bytecode Encryption & Symbol Stripping**:
   - Production binaries are stripped of debug symbols and function signatures.

### Frontend Protection (React / Electron)
1. **JavaScript Obfuscation & Minification**:
   - Terser minification combined with AST control-flow flattening, string array encryption, and variable mangling (`javascript-obfuscator`).
2. **Bytenode V8 Bytecode Compilation**:
   - Compiles JavaScript code into native V8 bytecode (`.jsc` files).
   - Prevents users from un-archiving `app.asar` to inspect readable JavaScript source files.
3. **Electron Security Lockdowns**:
   - DevTools inspection disabled in production (`webContents.on('devtools-opened', ...)`).
   - `contextIsolation` enabled and `nodeIntegration` disabled in renderers.

---

## 5. Directory Structure & Key Files

```
control-toolkit/
├── docs/
│   └── desktop-app-architecture.md  <-- This documentation file
├── backend/
│   ├── control_toolkit/             <-- Core Python backend source
│   └── scripts/
│       ├── build_sidecar.py         <-- Compiles Python backend sidecar binary
│       └── protect_code.py          <-- Nuitka / Cython compilation pipeline
└── frontend/
    ├── electron/
    │   ├── main.cjs                 <-- Cross-platform Electron main process
    │   └── preload.cjs              <-- Secure IPC preload script
    ├── package.json                 <-- Configured with electron-builder & obfuscation
    └── electron-builder.yml         <-- Windows NSIS & Linux AppImage build targets
```

---

## 6. Build Commands Quick Reference

| Action | Command | Output |
| :--- | :--- | :--- |
| **Electron Dev Mode** | `cd control-toolkit/frontend && npm run electron:dev` | Live-reloading desktop window |
| **Compile Python Sidecar** | `python control-toolkit/backend/scripts/build_sidecar.py` | `backend/dist/backend.exe` |
| **Build Windows Installer** | `cd control-toolkit/frontend && npm run electron:build -- --win` | `ControlToolkit-Setup.exe` |
| **Build Linux AppImage** | `cd control-toolkit/frontend && npm run electron:build -- --linux` | `ControlToolkit.AppImage` |
