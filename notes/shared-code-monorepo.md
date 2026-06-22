# Shared Code Architecture — Monorepo Embedded Pattern

When a system has **multiple MCUs** that must agree on protocol definitions (CAN IDs, frame layouts, enums), you need a **single source of truth** for those definitions — but you can't link them at runtime because each MCU is an independent physical device with its own flash.

The e-trike uses a **header-only shared library** consumed by each project via compiler include paths. No symlinks, no copies, no separate build artifact.

---

## The Problem

```
┌─────────────┐     CAN bus      ┌─────────────┐
│  RT ESP32   │◄═══════════════►│  SYS ESP32   │
└─────────────┘                  └─────────────┘

Both must agree on:
  • CAN message IDs (0x169 = steering, 0x205 = brake, …)
  • Frame layouts (which byte is speed, gear, mode)
  • Enum values (Mode::Auto = 1, Gear::D = 1)
  • Serialisation order (big-endian vs little-endian per field)
```

If you duplicate these definitions, they **will** drift apart — a field gets reordered in one file but not the other, and suddenly the brake command reads as a speed value on the other side.

---

## The Pattern: Header-Only Shared Library

```
project/
├── shared/               ← single source of truth
│   ├── can/
│   │   ├── can_protocol.h    # CAN IDs, Frame struct, all payload types
│   │   └── can_driver.h      # RAII TWAI driver (ESP-IDF dependent)
│   └── os/
│       ├── endian.h           # Big-endian read/write helpers
│       ├── queue.h            # Type-safe FreeRTOS queue wrapper
│       └── result.h           # Result<T,E> error handling
│
├── rt-esp32/             ← consumer A
│   ├── platformio.ini        # build_flags: -I ../shared
│   └── src/
│       └── main.cpp          # #include "can/can_protocol.h"
│
└── sys-esp32/            ← consumer B
    ├── platformio.ini        # build_flags: -I ../shared
    └── src/
        └── main.cpp          # #include "can/can_protocol.h"
```

### Key properties

1. **No `.cpp` files in `shared/`.** Everything is `inline`, `constexpr`, or `template`. The shared code compiles directly into each consumer's translation units — no shared object, no library to link.

2. **Include via path, not symlink.** Each `platformio.ini` adds `-I ../shared` to `build_flags`. When the compiler sees `#include "can/can_protocol.h"`, it searches the include path and finds `../shared/can/can_protocol.h`.

3. **Each MCU gets its own copy.** The shared code is compiled into `rt-esp32.bin` and into `sys-esp32.bin` independently. There's no runtime sharing — the CAN bus is the only communication channel between them.

---

## Why Header-Only?

| Constraint | Why it rules out alternatives |
|------------|-------------------------------|
| Embedded (no OS linker) | Can't load a `.so` or `.dll` at runtime |
| Separate flash chips | Can't share a memory-mapped code region |
| No filesystem on MCU | Can't distribute definitions as data files |
| Compile-time is free | `inline`/`constexpr` has zero runtime cost |

Header-only is the **lightest-weight sharing mechanism** available: the compiler does all the work, and the linker sees nothing new.

---

## The Include Path Mechanism

In PlatformIO (ESP-IDF framework):

```ini
; rt-esp32/platformio.ini (identical line in sys-esp32/platformio.ini)
build_flags =
    -I ../shared
```

This tells GCC/Clang: *"when resolving `#include` directives, also look in `../shared` relative to the project directory."*

The include chain works like this:

```
main.cpp                           shared/can/can_protocol.h
┌──────────────────────────┐       ┌──────────────────────────┐
│ #include "can/can_protocol.h"────►│ #include "os/endian.h"──┼──► shared/os/endian.h
│                          │       │                          │
│ // uses can::Frame       │       │ namespace can { … }     │
│ // uses can::Mode        │       └──────────────────────────┘
│ // uses can::VcuSesReq   │
└──────────────────────────┘
```

Each `#include` with quotes (`"…"`) checks the current directory first, then the `-I` paths. Angle-bracket includes (`<…>`) check only system paths — FreeRTOS and ESP-IDF headers use this.

---

## Optional: Per-Project Wrappers

When the shared code needs project-specific configuration, each consumer adds a thin wrapper:

```cpp
// sys-esp32/src/can_driver.h — NOT a copy, just configuration glue
#include "can/can_driver.h"   // the shared one
#include "config.h"           // sys-esp32's own pin definitions

namespace sys {
inline can::CanDriver::Config make_can_config() {
    can::CanDriver::Config cfg;
    cfg.tx_gpio    = sys::kCanTxGpio;    // 4 for SYS, 5 for RT
    cfg.rx_gpio    = sys::kCanRxGpio;
    cfg.bitrate_hz = sys::kCanBitrateHz;
    return cfg;
}
}
```

The wrapper **does not duplicate** the shared driver — it only fills in local constants. The heavy logic stays in `shared/`.

---

## How It Lands on Hardware

```
shared/                  rt-esp32/                  sys-esp32/
(headers only)           (consumer)                 (consumer)
                             │                          │
                    ┌────────▼────────┐      ┌─────────▼────────┐
                    │ pio run          │      │ pio run          │
                    │ g++ -I ../shared │      │ g++ -I ../shared │
                    │ … main.cpp       │      │ … main.cpp       │
                    └────────┬────────┘      └─────────┬────────┘
                             │                          │
                    ┌────────▼────────┐      ┌─────────▼────────┐
                    │ rt-esp32.elf    │      │ sys-esp32.elf    │
                    │ (shared code    │      │ (shared code     │
                    │  compiled in)   │      │  compiled in)    │
                    └────────┬────────┘      └─────────┬────────┘
                             │                          │
                    ┌────────▼────────┐      ┌─────────▼────────┐
                    │ pio run -t      │      │ pio run -t       │
                    │ upload          │      │ upload           │
                    └────────┬────────┘      └─────────┬────────┘
                             │                          │
                    ┌────────▼────────┐      ┌─────────▼────────┐
                    │ RT ESP32-S3     │      │ SYS ESP32-S3     │
                    │ (flash chip)    │      │ (flash chip)      │
                    └─────────────────┘      └──────────────────┘

                    Each MCU has its own independent
                    copy of the shared definitions.
                    They agree because they came from
                    the same source files.
```

---

## When This Pattern Works

| ✅ Good fit | ❌ Poor fit |
|-------------|-------------|
| Protocol definitions (CAN IDs, enums, frame layouts) | Large function bodies (bloats every consumer) |
| Lightweight inline helpers (serialisation, bit packing) | Stateful singletons (each MCU gets its own copy — that's usually wrong) |
| Template/constexpr utilities | Code that needs per-project conditional compilation (`#ifdef` hell) |
| 2–10 consumers in a monorepo | 50+ consumers (consider a proper library with versioning) |

---

## Alternatives

| Approach | When to use |
|----------|-------------|
| **Git submodule** | Shared code lives in a separate repo; consumers pin a specific commit. Good when the shared code has its own release cycle. |
| **Static library (.a)** | Shared code has `.cpp` files; you want to compile once and link. Works on embedded if the toolchain supports it, but adds build complexity. |
| **Code generation** | Protocol definitions are the source of truth (e.g., a DBC file); each project runs a generator to produce its own header. Avoids the include-path assumption entirely. |
| **Copy-paste** | Never. Don't. |

---

## Related Notes

- [[can-protocol]] — what the shared definitions actually define
- [[endianness-binary-protocols]] — why the serialisation helpers exist
- [[rtos-task-design]] — how the shared queue wrapper is used in tasks
- [[can-gateway-design]] — how RT bridges between the two CAN buses using these shared IDs
